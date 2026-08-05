#include "LiveKitConferenceProvider.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QUrl>
#include <QUuid>

#include <algorithm>

namespace orglink::server
{
namespace
{

/** @brief JWT 使用无填充 Base64URL，避免令牌作为 URL fragment 传递时需要二次转义。 */
QByteArray base64Url(const QByteArray& input)
{
    return input.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

} // namespace

LiveKitConferenceProvider::LiveKitConferenceProvider(LiveKitConfiguration configuration)
    : configuration_(std::move(configuration))
{
}

LiveKitConfiguration LiveKitConferenceProvider::configurationFromEnvironment(QString& diagnostic)
{
    LiveKitConfiguration configuration;
    configuration.apiKey = qEnvironmentVariable("ORGLINK_LIVEKIT_API_KEY");
    configuration.apiSecret = qEnvironmentVariable("ORGLINK_LIVEKIT_API_SECRET");
    configuration.serverUrl = qEnvironmentVariable("ORGLINK_LIVEKIT_PUBLIC_URL");
    configuration.webUrl = qEnvironmentVariable("ORGLINK_CONFERENCE_WEB_URL");
    bool ttlOk = false;
    const auto ttl = qEnvironmentVariable("ORGLINK_LIVEKIT_TOKEN_TTL_SECONDS", "600").toInt(&ttlOk);
    configuration.tokenTtlSeconds = ttlOk ? std::clamp(ttl, 60, 1800) : 600;
    if (configuration.apiKey.isEmpty() || configuration.apiSecret.size() < 16
        || !configuration.serverUrl.startsWith(QStringLiteral("ws"))
        || !QUrl(configuration.serverUrl).isValid() || !QUrl(configuration.webUrl).isValid())
    {
        diagnostic = QStringLiteral("LiveKit 配置缺失或无效");
    }
    return configuration;
}

protocol::ConferenceJoinResponse LiveKitConferenceProvider::issueJoinMaterial(
    const ConferenceJoinContext& context, bool videoEnabled) const
{
    protocol::ConferenceJoinResponse response;
    response.conferenceUuid = context.conferenceUuid;
    response.roomName = context.roomName;
    response.videoEnabled = videoEnabled;
    if (!context.success)
    {
        response.errorCode = context.errorCode;
        response.errorMessage = context.errorMessage;
        return response;
    }
    const auto nowSeconds = QDateTime::currentSecsSinceEpoch();
    const auto databaseExpirySeconds = static_cast<qint64>(context.expiresAtUtcMs / 1000U);
    const auto expiresSeconds = std::min(
        nowSeconds + static_cast<qint64>(configuration_.tokenTtlSeconds), databaseExpirySeconds);
    if (expiresSeconds <= nowSeconds || configuration_.apiSecret.isEmpty())
    {
        response.errorCode = 35003;
        response.errorMessage = "会议凭据暂时不可用";
        return response;
    }

    const QJsonObject header{{QStringLiteral("alg"), QStringLiteral("HS256")},
                             {QStringLiteral("typ"), QStringLiteral("JWT")}};
    const QJsonObject grants{{QStringLiteral("room"), QString::fromStdString(context.roomName)},
                             {QStringLiteral("roomJoin"), true},
                             {QStringLiteral("canPublish"), true},
                             {QStringLiteral("canSubscribe"), true},
                             {QStringLiteral("canPublishData"), true}};
    const QJsonObject payload{{QStringLiteral("iss"), configuration_.apiKey},
        {QStringLiteral("sub"), QString::fromStdString(context.participantIdentity)},
        {QStringLiteral("name"), QString::fromStdString(context.participantName)},
        {QStringLiteral("nbf"), nowSeconds - 5}, {QStringLiteral("exp"), expiresSeconds},
        {QStringLiteral("jti"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {QStringLiteral("video"), grants}};
    const auto signingInput = base64Url(QJsonDocument(header).toJson(QJsonDocument::Compact)) + '.'
        + base64Url(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    const auto signature = QMessageAuthenticationCode::hash(
        signingInput, configuration_.apiSecret.toUtf8(), QCryptographicHash::Sha256);
    response.success = true;
    response.serverUrl = configuration_.serverUrl.toStdString();
    response.webUrl = configuration_.webUrl.toStdString();
    response.participantToken = (signingInput + '.' + base64Url(signature)).toStdString();
    response.expiresAtUtcMs = static_cast<std::uint64_t>(expiresSeconds) * 1000U;
    return response;
}

} // namespace orglink::server
