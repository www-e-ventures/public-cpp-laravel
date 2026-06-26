// mail.hpp — mailer contract + array driver (Laravel: Illuminate\Contracts\Mail).
//
// MailerContract::send delivers a Mail. ArrayMailer records messages instead of
// sending — the Laravel "array" driver, ideal for asserting mail in tests. Header-only.
#pragma once
#include <string>
#include <vector>

struct Mail {
    std::string to;
    std::string subject;
    std::string body;
};

class MailerContract {
public:
    virtual ~MailerContract() = default;
    virtual void send(const Mail& message) = 0;
};

// Records messages instead of sending them.
class ArrayMailer : public MailerContract {
public:
    void send(const Mail& message) override { sent_.push_back(message); }

    const std::vector<Mail>& sent() const { return sent_; }
    bool sent_to(const std::string& address) const {
        for (const auto& m : sent_)
            if (m.to == address) return true;
        return false;
    }

private:
    std::vector<Mail> sent_;
};
