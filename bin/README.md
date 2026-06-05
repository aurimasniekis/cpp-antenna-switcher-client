# antenna-switcher-cli

[![CI](https://github.com/aurimasniekis/cpp-antenna-switcher-client/actions/workflows/ci.yml/badge.svg)](https://github.com/aurimasniekis/cpp-antenna-switcher-client/actions/workflows/ci.yml)
[![Docs](https://github.com/aurimasniekis/cpp-antenna-switcher-client/actions/workflows/docs.yml/badge.svg)](https://aurimasniekis.github.io/cpp-antenna-switcher-client/)

A command-line tool for the `antenna-switcher` ESP32 device over the ESPHome native API: run one-shot actions (set / auto / plan / stop / offset),
read state once, stream live updates, or open an interactive panel — with human-readable **text** or
machine-readable **JSON** output.

## Install

### Docker

```sh
docker run --rm --network host aurimasniekis/antenna-switcher-cli --help
docker run --rm --network host aurimasniekis/antenna-switcher-cli -k <psk> 10.28.0.2 state
```

`--network host` lets the container reach a device on your LAN. The image is distroless and the
binary is the entrypoint, so just append the usual arguments.

> Note: `interactive` mode needs a TTY — add `-it` (`docker run --rm -it --network host …
> interactive`).

### Homebrew

```sh
brew install aurimasniekis/tap/antenna-switcher-cli
```

### Linux packages (.deb / .rpm)

Each [release](https://github.com/aurimasniekis/cpp-antenna-switcher-client/releases) attaches `.deb`
and `.rpm` packages for `x86_64` and `arm64`. Download the one for your distro and architecture, then:

```sh
# Debian / Ubuntu
sudo apt install ./antenna-switcher-cli_0.4.1_amd64.deb

# Fedora / RHEL / openSUSE
sudo rpm -i antenna-switcher-cli-0.4.1-1.x86_64.rpm
```

The package installs `antenna-switcher-cli` to `/usr/bin`.

### From source

```sh
make cli
./build/bin/antenna-switcher-cli --help
# or
cmake -B build -DANTENNA_SWITCHER_BUILD_CLI=ON && cmake --build build --target antenna-switcher-cli
```

## Synopsis

```
antenna-switcher-cli [GLOBAL OPTIONS] [HOST] <command> [ARGS]
```

`HOST` is the device hostname/IP or a saved alias. It can be given as a positional argument,
`--host`, or the `ANTENNA_SWITCHER_CLI_HOST` environment variable. The `config` command does not need
a host.

```sh
antenna-switcher-cli 10.28.0.2 state
antenna-switcher-cli --host 10.28.0.2 state
ANTENNA_SWITCHER_CLI_HOST=10.28.0.2 antenna-switcher-cli state
```

## Global options

Every option takes a flag or an environment variable. Precedence is **flag > env > config > default**.

| Flag                             | Env var                          | Default  | Meaning                                 |
|----------------------------------|----------------------------------|----------|-----------------------------------------|
| `<host>` / `--host`              | `ANTENNA_SWITCHER_CLI_HOST`      | —        | Device host/IP or saved alias           |
| `-k`, `--key`                    | `ANTENNA_SWITCHER_CLI_KEY`       | —        | Base64 Noise PSK (enables encryption)   |
| `-p`, `--port`                   | `ANTENNA_SWITCHER_CLI_PORT`      | `6053`   | ESPHome native API port                 |
| `-c`, `--channel`                | `ANTENNA_SWITCHER_CLI_CHANNEL`   | `1`      | Target channel for actions (`1` or `2`) |
| `-o`, `--output`                 | `ANTENNA_SWITCHER_CLI_OUTPUT`    | `text`   | Output format: `text` or `json`         |
| `--log-level`                    | `ANTENNA_SWITCHER_CLI_LOG_LEVEL` | `warn`   | spdlog level on stderr                  |
| `--config`                       | `ANTENNA_SWITCHER_CLI_CONFIG`    | XDG path | Config file path                        |
| `--save-keys` / `--no-save-keys` | `ANTENNA_SWITCHER_CLI_SAVE_KEYS` | `true`   | Persist PSKs to the config              |
| `--timeout`                      | `ANTENNA_SWITCHER_CLI_TIMEOUT`   | `1500`   | Post-action settle wait, milliseconds   |
| `--version`                      | —                                | —        | Print version and exit                  |
| `-h`, `--help`                   | —                                | —        | Print help and exit                     |

`--log-level` accepts `trace`, `debug`, `info`, `warn`/`warning`, `error`/`err`, `critical`, `off`.

`--timeout` is the time an **action** command waits, after sending, for the channel to report its new
state before printing. It is not a connection timeout (connection is bounded by the underlying
library).

## Commands

| Command                                   | What it does                                    |
|-------------------------------------------|-------------------------------------------------|
| `set <input>`                             | Select an input (`1..10`) on the target channel |
| `auto <interval> [--us] [--inputs 1,2,3]` | Start auto-cycling                              |
| `plan <step>... [--repeat]`               | Run an ordered plan                             |
| `stop`                                    | Stop auto/plan activity                         |
| `offset <degrees>`                        | Set the compass angle offset (`0..359`)         |
| `state`                                   | Print a one-shot snapshot, then exit            |
| `watch [--channel N]`                     | Stream state changes until Ctrl-C               |
| `interactive`                             | Live ANSI panel + command REPL                  |
| `config list \| path \| forget <alias>`   | Manage saved devices                            |

The action commands (`set`, `auto`, `plan`, `stop`, `offset`) all follow the same flow: connect →
send the command → wait up to `--timeout` for the channel to confirm the new state → print the
resulting state → disconnect. On a successful connection the device is remembered in the config.

### `set`

```sh
antenna-switcher-cli -k <psk> 10.28.0.2 set 5          # input 5 on channel #1
antenna-switcher-cli -k <psk> -c 2 10.28.0.2 set 3     # input 3 on channel #2
```

Input must be `1..10`; out-of-range exits with code `2`.

### `auto`

```sh
# Cycle inputs 1,2,3 every 250 ms
antenna-switcher-cli -k <psk> 10.28.0.2 auto 250 --inputs 1,2,3

# Cycle every input every 5000 microseconds
antenna-switcher-cli -k <psk> 10.28.0.2 auto 5000 --us

# Cycle every input every 250 ms (no --inputs means all)
antenna-switcher-cli -k <psk> 10.28.0.2 auto 250
```

`--us` interprets the interval as microseconds (default is milliseconds). `--inputs` is a CSV of
input numbers (`1..10`). A list of 1..9 inputs sets an explicit cycle order; an omitted list or all
ten inputs cycles everything.

### `plan`

```sh
antenna-switcher-cli -k <psk> 10.28.0.2 plan 1 100ms 2 50us --repeat
```

Each step token is either a bare integer (select that input) or a delay written as `<n>ms` or
`<n>us`. `--repeat` loops the plan. The example above sends `plan:1,s100,2,s50u,r`. A malformed token
(e.g. `100xx`) or an out-of-range input exits with code `2`.

### `stop`

```sh
antenna-switcher-cli -k <psk> 10.28.0.2 stop
```

### `offset`

```sh
antenna-switcher-cli -k <psk> 10.28.0.2 offset 45
```

Degrees must be `0..359`. This writes the device's angle-offset `number` entity.

### `state`

```sh
antenna-switcher-cli -k <psk> 10.28.0.2 state          # both channels
antenna-switcher-cli -k <psk> -c 2 10.28.0.2 state     # only channel #2
antenna-switcher-cli -k <psk> -o json 10.28.0.2 state  # JSON
```

With `-c`/`--channel`, only that channel is shown and JSON output is a single object; without it,
both channels are shown and JSON output is an array.

### `watch`

```sh
antenna-switcher-cli -k <psk> 10.28.0.2 watch                 # both channels, text
antenna-switcher-cli -k <psk> -o json 10.28.0.2 watch         # NDJSON stream
antenna-switcher-cli -k <psk> 10.28.0.2 watch --channel 1     # only channel #1
```

Prints the current snapshot up front, then streams each `onStateChanged` event until you press
Ctrl-C (SIGINT) or the link drops. In `text` mode each update is one line; in `json` mode it is
newline-delimited JSON (one compact object per line, flushed) — convenient to pipe into `jq`:

```sh
antenna-switcher-cli -k <psk> -o json 10.28.0.2 watch | jq '.activeInput'
```

### `interactive`

```sh
antenna-switcher-cli -k <psk> 10.28.0.2 interactive
```

Opens a live two-line status panel (one row per channel) that repaints in place as updates arrive,
above a `> ` prompt that stays editable. The REPL accepts:

```
set <n>                 select input n (1..10)
auto <iv> [csv] [us]    auto-cycle, e.g. "auto 250 1,2,3" or "auto 5000 us"
plan <step>... [repeat] e.g. "plan 1 100ms 2 50us repeat"
stop                    stop auto/plan
offset <deg>            set the angle offset (0..359)
channel <1|2>           switch the target channel
state                   reprint the panel
help                    show command help
quit                    disconnect and exit (also Ctrl-D)
```

`set`/`auto`/`plan`/`stop`/`offset` act on the current channel (start from `-c`, change it with
`channel`). Invalid commands print an `error:` line and leave the session running.

### `config`

```sh
antenna-switcher-cli config list           # defaults + saved devices
antenna-switcher-cli config path           # print the config file path
antenna-switcher-cli config forget <alias> # drop a saved device
```

These do not connect to a device.

## Saved devices and the config file

On a successful connection, the host is remembered (address, port, and — unless `--no-save-keys` —
the PSK). Afterwards a saved **alias** can stand in for the host and key:

```sh
# First time: full details, saved on success
antenna-switcher-cli -k <psk> 10.28.0.2 state

# Later: the alias resolves address + port + PSK
antenna-switcher-cli 10.28.0.2 state
```

An alias matches by config key, then by address, then by friendly name.

The config is JSON at `$XDG_CONFIG_HOME/antenna-switcher-cli/config.json`, falling back to
`~/.config/antenna-switcher-cli/config.json`. It is written with mode `0600`. The first time a PSK is
stored, a one-time plaintext-storage notice is printed to stderr; use `--no-save-keys` to avoid
storing keys.

## Output formats

Select with `-o`/`--output` (or `ANTENNA_SWITCHER_CLI_OUTPUT`).

A channel state serializes as:

```json
{
  "channel": 1,
  "mode": "auto",
  "activeInput": 2,
  "bearing": 137,
  "angleOffset": 45,
  "intervalUs": 250000,
  "activeInputs": [1, 2, 3]
}
```

An action result wraps the state with the exact command that was sent:

```json
{
  "channel": 1,
  "sent": "set:5",
  "state": { "channel": 1, "mode": "manual", "activeInput": 5, "...": "..." }
}
```

`state` without `-c` emits a JSON array of both channels; `watch` emits one compact object per line
(NDJSON). `mode` is one of `manual`, `auto`, `plan`, `unknown`.

## Exit codes

| Code | Meaning                                                                            |
|------|------------------------------------------------------------------------------------|
| `0`  | Success                                                                            |
| `2`  | Usage error (bad arguments, out-of-range value, missing host, parse error)         |
| `3`  | Authentication / encryption / handshake failure (often a wrong or missing `--key`) |
| `4`  | Connection failure or timeout (refused, unreachable, unresolved)                   |
| `5`  | Other device/protocol error                                                        |
| `1`  | Unexpected error                                                                   |

```sh
antenna-switcher-cli 127.0.0.1 state            # connection refused
echo $?                                          # -> 4
```

## Examples

```sh
psk=0a2wipu2cBWSNiaJ2z4bYvdCaRTcgPaJtS535m3IP1g=
host=10.28.0.2

# Inspect
antenna-switcher-cli -k "$psk" "$host" state
antenna-switcher-cli -k "$psk" -o json "$host" state | jq

# Manual control
antenna-switcher-cli -k "$psk" "$host" set 5
antenna-switcher-cli -k "$psk" -c 2 "$host" set 3
antenna-switcher-cli -k "$psk" "$host" offset 45

# Auto / plan
antenna-switcher-cli -k "$psk" "$host" auto 250 --inputs 1,2,3
antenna-switcher-cli -k "$psk" "$host" plan 1 100ms 2 50us --repeat
antenna-switcher-cli -k "$psk" "$host" stop

# Stream and watch
antenna-switcher-cli -k "$psk" "$host" watch
antenna-switcher-cli -k "$psk" -o json "$host" watch | jq -c '{ch:.channel, in:.activeInput}'

# Live panel
antenna-switcher-cli -k "$psk" "$host" interactive

# After the first connect, the alias remembers address + key
antenna-switcher-cli "$host" state
```

## Contributing

Contributions to the library are welcome! If you encounter any issues or have suggestions for
improvements,
please feel free to submit a pull request or open an issue on the project's repository.

## License

This project is licensed under the MIT License. See the [LICENSE](../LICENSE) file for details.
