// smtp.hpp — an SMTP mail transport (Laravel: the "smtp" mailer).
//
// format_email() and smtp_commands() are pure (unit-tested); SmtpMailer::send opens a
// socket and runs the conversation (best-effort, untested without a live server — like
// the dev HTTP server's socket loop). ArrayMailer (mail.hpp) remains the test default.
#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "mail.hpp"

// RFC-822-ish message body (headers + blank line + body).
inline std::string format_email(const std::string& from, const Mail& m) {
    return "From: " + from + "\r\nTo: " + m.to + "\r\nSubject: " + m.subject + "\r\n\r\n" + m.body +
           "\r\n";
}

// The minimal SMTP command sequence for one message.
inline std::vector<std::string> smtp_commands(const std::string& from, const Mail& m) {
    return {
        "HELO localhost",
        "MAIL FROM:<" + from + ">",
        "RCPT TO:<" + m.to + ">",
        "DATA",
        format_email(from, m) + ".", // the lone '.' terminates DATA
        "QUIT",
    };
}

class SmtpMailer : public MailerContract {
public:
    SmtpMailer(std::string host, int port, std::string from)
        : host_(std::move(host)), port_(port), from_(std::move(from)) {}

    // Best-effort: connect and push the commands. Silently no-ops on failure.
    void send(const Mail& message) override {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port_));
        ::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            char buf[256];
            (void)!::recv(fd, buf, sizeof(buf), 0); // greeting
            for (const auto& cmd : smtp_commands(from_, message)) {
                std::string line = cmd + "\r\n";
                (void)!::send(fd, line.data(), line.size(), 0);
                (void)!::recv(fd, buf, sizeof(buf), 0);
            }
        }
        ::close(fd);
    }

private:
    std::string host_;
    int port_;
    std::string from_;
};
