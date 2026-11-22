#include "sunlens/subscription-manager.hpp"

#include <QHostAddress>
#include <QNetworkRequest>
#include <algorithm>

namespace sunlens {

SubscriptionManager::SubscriptionManager(ProfileRepository& repository, FetchPolicy policy,
                                         QObject* parent)
    : QObject(parent), repository_(repository), policy_(policy) {
    network_.setAutoDeleteReplies(false);
    policy_.maxBytes = std::clamp(policy_.maxBytes, qsizetype(1), ProfileParser::MaxBytes);
    policy_.retries = std::clamp(policy_.retries, 0, 2);
    policy_.timeoutMs = std::clamp(policy_.timeoutMs, 10, 60000);
    deadline_.setSingleShot(true);
    retry_.setSingleShot(true);
    connect(&retry_, &QTimer::timeout, this, &SubscriptionManager::attempt);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        failureReason_ = QStringLiteral("Истекло время загрузки подписки. Проверьте сеть.");
        if (reply_)
            reply_->abort();
    });
}

SubscriptionManager::~SubscriptionManager() {
    retry_.stop();
    deadline_.stop();
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
        releaseReply();
    }
}

bool SubscriptionManager::busy() const { return active_; }

QUrl SubscriptionManager::checkedUrl(const QString& source) const {
    const QUrl url(source.trimmed(), QUrl::StrictMode);
    QHostAddress address(url.host());
    const bool localHttp =
        policy_.allowLoopbackHttp && address.isLoopback() && url.scheme() == "http";
    if (!url.isValid() || url.host().isEmpty() || (!localHttp && url.scheme() != "https") ||
        !url.userInfo().isEmpty() || url.hasFragment() || source.size() > 8192)
        throw Error(QStringLiteral(
            "Укажите HTTPS URL без userinfo и фрагмента. Токен допустим в пути/параметрах."));
    return url;
}

void SubscriptionManager::fetch(const QString& source, const QString& name) {
    if (active_) {
        emit failed(QStringLiteral("Дождитесь окончания загрузки или отмените её."));
        return;
    }
    try {
        const auto url = checkedUrl(source);
        const auto id = repository_.ensureSubscription(url.toString(QUrl::FullyEncoded), name);
        begin(id, url);
    } catch (const Error& error) {
        emit failed(QString::fromUtf8(error.what()));
    }
}

void SubscriptionManager::refresh(const QString& id) {
    if (active_) {
        emit failed(QStringLiteral("Уже выполняется загрузка подписки."));
        return;
    }
    try {
        const auto subscription = repository_.subscription(id);
        if (!subscription || subscription->url.isEmpty())
            throw Error(QStringLiteral("Эта группа не является удалённой подпиской."));
        begin(id, checkedUrl(subscription->url));
    } catch (const Error& error) {
        emit failed(QString::fromUtf8(error.what()));
    }
}

void SubscriptionManager::begin(const QString& id, const QUrl& url) {
    id_ = id;
    url_ = url;
    attempts_ = 0;
    active_ = true;
    emit busyChanged();
    attempt();
}

void SubscriptionManager::attempt() {
    ++attempts_;
    body_.clear();
    failureReason_.clear();
    QNetworkRequest request(url_);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setRawHeader("Accept", "text/plain, application/octet-stream");
    request.setRawHeader("User-Agent", "SUNLENS");
    reply_ = network_.get(request);
    reply_->setReadBufferSize(policy_.maxBytes + 1);
    connect(reply_, &QIODevice::readyRead, this, [this] {
        if (!reply_)
            return;
        body_ += reply_->read(policy_.maxBytes + 1 - body_.size());
        if (body_.size() > policy_.maxBytes) {
            failureReason_ = QStringLiteral("Подписка превышает допустимый размер.");
            reply_->abort();
        }
    });
    connect(reply_, &QNetworkReply::metaDataChanged, this, [this] {
        if (reply_ &&
            reply_->header(QNetworkRequest::ContentLengthHeader).toLongLong() > policy_.maxBytes) {
            failureReason_ = QStringLiteral("Подписка превышает допустимый размер.");
            reply_->abort();
        }
    });
    connect(reply_, &QNetworkReply::finished, this, &SubscriptionManager::finishReply);
    deadline_.start(policy_.timeoutMs);
}

void SubscriptionManager::releaseReply() {
    deadline_.stop();
    if (reply_) {
        reply_->disconnect(this);
        reply_->deleteLater();
        reply_ = nullptr;
    }
}

void SubscriptionManager::finishReply() {
    if (!reply_)
        return;
    const int status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto networkError = reply_->error();
    if (reply_->isOpen())
        body_ += reply_->read(policy_.maxBytes + 1 - body_.size());
    const auto failure = failureReason_;
    releaseReply();
    if (body_.size() > policy_.maxBytes) {
        fail(QStringLiteral("Подписка превышает допустимый размер."));
        return;
    }
    const bool transient = status == 429 || status >= 500 ||
                           networkError == QNetworkReply::TemporaryNetworkFailureError ||
                           networkError == QNetworkReply::TimeoutError;
    if (failure.isEmpty() && transient && attempts_ <= policy_.retries) {
        retry_.start(250 * attempts_);
        return;
    }
    if (!failure.isEmpty()) {
        fail(failure);
        return;
    }
    if (status >= 300 && status < 400) {
        fail(QStringLiteral("Перенаправление не принято. Укажите конечный HTTPS URL подписки."));
        return;
    }
    if (status != 200 || networkError != QNetworkReply::NoError) {
        fail(status > 0 ? QStringLiteral("Ошибка HTTP %1 при загрузке подписки.").arg(status)
                        : QStringLiteral("Не удалось загрузить подписку. Проверьте сеть и TLS."));
        return;
    }
    try {
        const auto nodes = ProfileParser::parse(body_);
        repository_.replaceNodes(id_, nodes);
        body_.fill('\0');
        body_.clear();
        active_ = false;
        emit busyChanged();
        emit completed(id_);
    } catch (const Error& error) {
        fail(QString::fromUtf8(error.what()));
    }
}

void SubscriptionManager::fail(const QString& message) {
    releaseReply();
    retry_.stop();
    body_.fill('\0');
    body_.clear();
    try {
        repository_.markFetchError(id_);
    } catch (const Error&) {
        // Ошибка записи статуса не должна скрывать исходную причину сбоя.
    }
    active_ = false;
    emit busyChanged();
    emit failed(message);
}

void SubscriptionManager::cancel() {
    if (!active_)
        return;
    retry_.stop();
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
    }
    fail(QStringLiteral("Загрузка отменена. Ранее сохранённые узлы не изменены."));
}

} // namespace sunlens
