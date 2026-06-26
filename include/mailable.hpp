// mailable.hpp — build an email from a Blade view (Laravel: a Mailable with ->view()).
//
// Renders a named template from a Views registry with the given data and wraps it as a
// Mail, ready for any MailerContract::send.
#pragma once
#include <string>

#include "mail.hpp"
#include "view.hpp"

inline Mail mailable(const Views& views, const std::string& to, const std::string& subject,
                     const std::string& view, const View& data) {
    Mail m;
    m.to = to;
    m.subject = subject;
    m.body = views.render(view, data);
    return m;
}
