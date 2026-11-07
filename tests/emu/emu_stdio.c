/**
 * @file    emu_stdio.c
 * @brief   Native front-end of the device emulator.
 *
 *   miniquad-emu [--power-on] [--speed X]
 *
 *   stdin  : bytes from the host (as on the UART RX pin)
 *   stdout : bytes to the host (UART TX, 460800 baud pacing)
 *   fd 3   : optional control channel, one text command per line:
 *              rc <ch 1..8> <us>   tx <0|1>      tof <0|1>
 *              boot0 <0|1>         power         reset
 *              vehicle             battery <soc> kick <r> <p> <y>
 *              fault               run <ms>      state     quit
 *              usb <0|1>           (USB cable, board rev 2)
 *   stderr : "STATE {json}" answers, mode changes ("MODE app") and host
 *            port changes ("LINK 1 <epoch>" / "LINK 0 <epoch>": on board
 *            rev 2 the USB serial port vanishes on every MCU reset)
 *
 * Time runs in real time (times --speed) so the protocol timeouts of the
 * host behave as with hardware.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "emu_device.h"

static const char *mode_name(emu_mode_t m)
{
    switch (m) {
    case EMU_MODE_APP:        return "app";
    case EMU_MODE_BOOTLOADER: return "bootloader";
    case EMU_MODE_ROM:        return "rom";
    default:                  return "dead";
    }
}

static double wall_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

static void print_state(void)
{
    float t[14];
    emu_truth(t, 14);
    fprintf(stderr,
            "STATE {\"mode\":\"%s\",\"uptime\":%u,\"armed\":%d,\"x\":%.3f,\"y\":%.3f,\"h\":%.3f,"
            "\"roll\":%.2f,\"pitch\":%.2f,\"yaw\":%.2f,\"vbat\":%.3f,\"current\":%.3f,"
            "\"motors\":[%.3f,%.3f,%.3f,%.3f],\"soc\":%.3f,\"led\":%d,\"buzzer\":%u,"
            "\"rev\":%d,\"link\":%d,\"epoch\":%u}\n",
            mode_name(emu_mode()), emu_uptime_ms(), (int)t[8], t[0], t[1], t[2], t[3], t[4], t[5], t[6],
            t[7], t[9], t[10], t[11], t[12], t[13], emu_led() ? 1 : 0, emu_buzzer_hz(), emu_board_rev(),
            emu_link_up() ? 1 : 0, emu_link_epoch());
    fflush(stderr);
}

/* returns false on "quit"; *skip receives a fast-forward request */
static int command(char *line, double *skip_ms)
{
    char cmd[16] = { 0 };
    double a = 0, b = 0, c = 0;
    int n = sscanf(line, "%15s %lf %lf %lf", cmd, &a, &b, &c);
    if (n < 1) {
        return 1;
    }
    if (strcmp(cmd, "rc") == 0 && n >= 3) {
        emu_rc_set((int)a - 1, (uint16_t)b);
    } else if (strcmp(cmd, "tx") == 0 && n >= 2) {
        emu_rc_enable(a != 0);
    } else if (strcmp(cmd, "tof") == 0 && n >= 2) {
        emu_tof_enable(a != 0);
    } else if (strcmp(cmd, "boot0") == 0 && n >= 2) {
        emu_set_boot0(a != 0);
    } else if (strcmp(cmd, "power") == 0) {
        emu_power_cycle();
    } else if (strcmp(cmd, "reset") == 0) {
        emu_reset_pin();
    } else if (strcmp(cmd, "vehicle") == 0) {
        emu_vehicle_reset();
    } else if (strcmp(cmd, "battery") == 0 && n >= 2) {
        emu_battery_set((float)a);
    } else if (strcmp(cmd, "kick") == 0 && n >= 4) {
        emu_kick((float)a, (float)b, (float)c);
    } else if (strcmp(cmd, "usb") == 0 && n >= 2) {
        emu_usb_cable(a != 0);
    } else if (strcmp(cmd, "fault") == 0) {
        emu_inject_fault();
    } else if (strcmp(cmd, "run") == 0 && n >= 2) {
        *skip_ms += a;
    } else if (strcmp(cmd, "state") == 0) {
        print_state();
    } else if (strcmp(cmd, "quit") == 0) {
        return 0;
    } else {
        fprintf(stderr, "ERR unknown command: %s\n", line);
    }
    return 1;
}

int main(int argc, char **argv)
{
    int power_on = 0;
    double speed = 1.0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--power-on") == 0) {
            power_on = 1;
        } else if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc) {
            speed = atof(argv[++i]);
            if (speed <= 0.0) {
                speed = 1.0;
            }
        } else {
            fprintf(stderr, "usage: %s [--power-on] [--speed X]\n", argv[0]);
            return 2;
        }
    }
    int ctrl = (fcntl(3, F_GETFD) >= 0) ? 3 : -1;
    if (ctrl >= 0) {
        fcntl(ctrl, F_SETFL, fcntl(ctrl, F_GETFL) | O_NONBLOCK);
    }

    emu_init(!power_on);
    emu_mode_t last_mode = emu_mode();
    bool last_link = emu_link_up();
    uint32_t last_epoch = emu_link_epoch();
    fprintf(stderr, "BOARD %d\nMODE %s\nLINK %d %u\n", emu_board_rev(), mode_name(last_mode),
            last_link ? 1 : 0, last_epoch);
    fflush(stderr);

    char line[256];
    size_t line_n = 0u;
    double t0 = wall_ms();
    double emu_ms = 0.0;
    int running = 1;
    int stdin_open = 1;

    while (running) {
        struct pollfd pf[2];
        int nfds = 0;
        if (stdin_open) {
            pf[nfds].fd = 0;
            pf[nfds].events = POLLIN;
            nfds++;
        }
        if (ctrl >= 0) {
            pf[nfds].fd = ctrl;
            pf[nfds].events = POLLIN;
            nfds++;
        }
        (void)poll(pf, (nfds_t)nfds, 1);

        for (int i = 0; i < nfds; i++) {
            if ((pf[i].revents & (POLLIN | POLLHUP)) == 0) {
                continue;
            }
            uint8_t buf[4096];
            ssize_t n = read(pf[i].fd, buf, sizeof(buf));
            if (pf[i].fd == 0) {
                if (n > 0) {
                    emu_rx(buf, (size_t)n);
                } else if (n == 0) {
                    stdin_open = 0;         /* host closed the port */
                    running = 0;
                }
            } else if (n > 0) {
                double skip = 0.0;
                for (ssize_t k = 0; k < n; k++) {
                    char ch = (char)buf[k];
                    if (ch == '\n' || line_n == sizeof(line) - 1u) {
                        line[line_n] = '\0';
                        line_n = 0u;
                        if (!command(line, &skip)) {
                            running = 0;
                        }
                    } else if (ch != '\r') {
                        line[line_n++] = ch;
                    }
                }
                if (skip > 0.0) {
                    emu_run((uint32_t)skip);
                    emu_ms += skip;
                    t0 -= skip / speed;     /* keep the real-time pacing */
                }
            } else if (n == 0) {
                ctrl = -1;
            }
        }

        double target = (wall_ms() - t0) * speed;
        int steps = (int)(target - emu_ms);
        if (steps > 50) {
            steps = 50;                     /* never race to catch up */
            emu_ms = target - 50.0;
        }
        if (steps > 0) {
            emu_run((uint32_t)steps);
            emu_ms += steps;
        }

        uint8_t out[8192];
        size_t n;
        while ((n = emu_tx(out, sizeof(out))) > 0u) {
            size_t off = 0u;
            while (off < n) {
                ssize_t w = write(1, out + off, n - off);
                if (w < 0) {
                    if (errno == EINTR || errno == EAGAIN) {
                        continue;
                    }
                    return 1;
                }
                off += (size_t)w;
            }
        }
        if (emu_mode() != last_mode) {
            last_mode = emu_mode();
            fprintf(stderr, "MODE %s\n", mode_name(last_mode));
            fflush(stderr);
        }
        if (emu_link_up() != last_link || emu_link_epoch() != last_epoch) {
            if (last_link && emu_link_epoch() != last_epoch && emu_link_up()) {
                /* went down and up between two passes: report both */
                fprintf(stderr, "LINK 0 %u\n", emu_link_epoch());
            }
            last_link = emu_link_up();
            last_epoch = emu_link_epoch();
            fprintf(stderr, "LINK %d %u\n", last_link ? 1 : 0, last_epoch);
            fflush(stderr);
        }
    }
    return 0;
}
