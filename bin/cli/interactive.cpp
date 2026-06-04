#include "cli/interactive.hpp"

#include "cli/commands.hpp"
#include "cli/output.hpp"

#include <array>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace asw_cli {

namespace as = antenna_switcher;

namespace {

// ANSI control sequences (DEC save/restore cursor + line editing). The escape
// and the trailing digit are kept as separate string literals so the digit does
// not extend the octal escape (and so MSVC's C4125 stays quiet).
constexpr auto save_cursor = "\033"
                             "7";
constexpr auto restore_cursor = "\033"
                                "8";
constexpr auto cursor_up_3 = "\033[3A";
constexpr auto cursor_down_1 = "\033[1B";
constexpr auto clear_line = "\r\033[2K";

std::size_t channel_index(const as::Channel ch) {
    return ch == as::Channel::Two ? 1U : 0U;
}

/// Owns the live panel + REPL state for one interactive session.
class Session {
public:
    Session(const CliContext& ctx, as::AntennaSwitcher& dev)
        : dev_(dev), current_(static_cast<int>(ctx.channel())) {}

    void on_state(const as::Channel ch, const as::ChannelState& s) {
        {
            const std::scoped_lock lk(state_mtx_);
            states_.at(channel_index(ch)) = s;
        }
        repaint();
    }

    int repl() {
        seed_states();
        print_panel();
        std::string line;
        while (std::getline(std::cin, line)) {
            {
                const std::scoped_lock lk(out_mtx_);
                prompt_shown_ = false;
            }
            if (!process(line))
                break;
            print_panel();
        }
        // A trailing newline so the shell prompt starts cleanly after EOF/quit.
        const std::scoped_lock lk(out_mtx_);
        std::cout << "\n";
        std::cout.flush();
        return 0;
    }

private:
    void seed_states() {
        const std::scoped_lock lk(state_mtx_);
        states_.at(0) = dev_.state(as::Channel::One);
        states_.at(1) = dev_.state(as::Channel::Two);
    }

    std::array<std::string, 2> panel_lines() {
        const std::scoped_lock lk(state_mtx_);
        return {channel_state_to_text(as::Channel::One, states_.at(0)),
                channel_state_to_text(as::Channel::Two, states_.at(1))};
    }

    void print_panel() {
        const std::array<std::string, 2> lines = panel_lines();
        const std::scoped_lock lk(out_mtx_);
        std::cout << lines.at(0) << "\n" << lines.at(1) << "\n\n";
        std::cout << prompt();
        std::cout.flush();
        prompt_shown_ = true;
    }

    void repaint() {
        const std::array<std::string, 2> lines = panel_lines();
        const std::scoped_lock lk(out_mtx_);
        if (!prompt_shown_)
            return;
        std::cout << save_cursor << cursor_up_3;
        std::cout << clear_line << lines.at(0);
        std::cout << cursor_down_1 << clear_line << lines.at(1);
        std::cout << restore_cursor;
        std::cout.flush();
    }

    [[nodiscard]] std::string prompt() const {
        return "[#" + std::to_string(current_) + "] > ";
    }

    void print_msg(const std::string& msg) {
        const std::scoped_lock lk(out_mtx_);
        std::cout << msg << "\n";
        std::cout.flush();
    }

    static void print_help() {
        std::cout << "commands:\n"
                  << "  set <n>                 select input n (1..10)\n"
                  << "  auto <iv> [csv] [us]    auto-cycle (e.g. auto 250 1,2,3)\n"
                  << "  plan <step>... [repeat] plan (e.g. plan 1 100ms 2 50us repeat)\n"
                  << "  stop                    stop auto/plan\n"
                  << "  offset <deg>            set angle offset (0..359)\n"
                  << "  channel <1|2>           switch the target channel\n"
                  << "  state                   reprint the panel\n"
                  << "  help                    show this help\n"
                  << "  quit                    disconnect and exit\n";
    }

    as::Channel target() const {
        return current_ == 2 ? as::Channel::Two : as::Channel::One;
    }

    /// Returns false to request exit.
    bool process(const std::string& line) {
        std::istringstream ss(line);
        std::vector<std::string> tok;
        std::string w;
        while (ss >> w)
            tok.push_back(w);
        if (tok.empty())
            return true;

        const std::string& verb = tok.front();
        try {
            if (verb == "quit" || verb == "exit" || verb == "q")
                return false;
            if (verb == "help" || verb == "?") {
                const std::scoped_lock lk(out_mtx_);
                print_help();
                return true;
            }
            if (verb == "state")
                return true;  // panel reprints after process() returns
            if (verb == "channel") {
                if (tok.size() < 2)
                    throw std::runtime_error("usage: channel <1|2>");
                const int n = std::stoi(tok.at(1));
                if (n != 1 && n != 2)
                    throw std::runtime_error("channel must be 1 or 2");
                current_ = n;
                return true;
            }
            if (verb == "set") {
                if (tok.size() < 2)
                    throw std::runtime_error("usage: set <n>");
                const int n = std::stoi(tok.at(1));
                if (n < 1 || n > 10)
                    throw std::runtime_error("input out of range (1..10)");
                dev_.setInput(target(), n);
                print_msg("ok: " + as::detail::build_set_input(n));
                return true;
            }
            if (verb == "auto") {
                if (tok.size() < 2)
                    throw std::runtime_error("usage: auto <interval> [csv] [us]");
                const int iv = std::stoi(tok.at(1));
                bool micros = false;
                std::vector<int> inputs;
                for (std::size_t i = 2; i < tok.size(); ++i) {
                    if (tok.at(i) == "us" || tok.at(i) == "--us")
                        micros = true;
                    else
                        inputs = parse_inputs_csv(tok.at(i));
                }
                const as::TimeUnit unit = micros ? as::TimeUnit::Us : as::TimeUnit::Ms;
                dev_.startAuto(target(), iv, unit, inputs);
                print_msg("ok: " + as::detail::build_start_auto(iv, unit, inputs));
                return true;
            }
            if (verb == "plan") {
                std::vector<std::string> steps;
                bool repeat = false;
                for (std::size_t i = 1; i < tok.size(); ++i) {
                    if (tok.at(i) == "repeat" || tok.at(i) == "--repeat" || tok.at(i) == "r")
                        repeat = true;
                    else
                        steps.push_back(tok.at(i));
                }
                const std::vector<as::PlanStep> parsed = parse_plan_steps(steps);
                dev_.runPlan(target(), parsed, repeat);
                print_msg("ok: " + as::detail::build_run_plan(parsed, repeat));
                return true;
            }
            if (verb == "stop") {
                dev_.stop(target());
                print_msg("ok: " + as::detail::build_stop());
                return true;
            }
            if (verb == "offset") {
                if (tok.size() < 2)
                    throw std::runtime_error("usage: offset <deg>");
                const int deg = std::stoi(tok.at(1));
                if (deg < 0 || deg > 359)
                    throw std::runtime_error("offset out of range (0..359)");
                dev_.setAngleOffset(target(), deg);
                print_msg("ok: angle_offset=" + std::to_string(deg));
                return true;
            }
            print_msg("unknown command '" + verb + "' (try 'help')");
        } catch (const std::exception& e) {
            print_msg(std::string("error: ") + e.what());
        }
        return true;
    }

    as::AntennaSwitcher& dev_;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    std::mutex out_mtx_;
    std::mutex state_mtx_;
    std::array<as::ChannelState, 2> states_;
    bool prompt_shown_ = false;
    int current_ = 1;
};

}  // namespace

int run_interactive(CliContext& ctx, const std::string& host) {
    const as::Options opt = resolve_options(ctx, host);
    try {
        as::AntennaSwitcher dev(opt);
        Session session(ctx, dev);
        dev.onStateChanged(
            [&](const as::Channel ch, const as::ChannelState& s) { session.on_state(ch, s); });

        dev.connect();
        persist_connection(ctx, host, opt);

        std::cout << "connected to " << opt.host << " — type 'help', 'quit' to exit\n";
        const int rc = session.repl();
        dev.disconnect();
        return rc;
    } catch (const std::exception& e) {
        return report_connect_error(ctx, host, e);
    }
}

}  // namespace asw_cli
