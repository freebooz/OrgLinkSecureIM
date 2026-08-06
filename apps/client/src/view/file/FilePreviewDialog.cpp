#include "view/file/FilePreviewDialog.h"

#include "view/common/UiAssets.h"

#include <QAudioOutput>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMediaPlayer>
#include <QMimeDatabase>
#include <QPushButton>
#include <QResizeEvent>
#include <QSlider>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>

#include <algorithm>

namespace orglink::client
{
namespace
{

/** @brief 判断扩展名是否可能执行代码；即使 MIME 伪装成图片也必须优先阻止。 */
bool isBlockedSuffix(const QString& suffix)
{
    static const QSet<QString> blocked{
        QStringLiteral("exe"), QStringLiteral("com"), QStringLiteral("bat"),
        QStringLiteral("cmd"), QStringLiteral("ps1"), QStringLiteral("psm1"),
        QStringLiteral("msi"), QStringLiteral("msix"), QStringLiteral("scr"),
        QStringLiteral("dll"), QStringLiteral("cpl"), QStringLiteral("lnk"),
        QStringLiteral("url"), QStringLiteral("reg"), QStringLiteral("js"),
        QStringLiteral("jse"), QStringLiteral("vbs"), QStringLiteral("vbe"),
        QStringLiteral("wsf"), QStringLiteral("wsh"), QStringLiteral("hta"),
        QStringLiteral("appx"), QStringLiteral("appxbundle")};
    return blocked.contains(suffix.toLower());
}

/** @brief 将毫秒时间格式化为媒体控制条使用的 mm:ss 或 hh:mm:ss。 */
QString playbackTime(qint64 milliseconds)
{
    const auto seconds = std::max<qint64>(0, milliseconds / 1000);
    const auto hours = seconds / 3600;
    const auto minutes = (seconds % 3600) / 60;
    const auto remainingSeconds = seconds % 60;
    return hours > 0
        ? QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0'))
              .arg(remainingSeconds, 2, 10, QLatin1Char('0'))
        : QStringLiteral("%1:%2").arg(minutes).arg(remainingSeconds, 2, 10, QLatin1Char('0'));
}

} // namespace

FilePreviewKind FilePreviewDialog::previewKind(const QString& localPath, const QString& mediaType)
{
    const QFileInfo fileInfo(localPath);
    if (isBlockedSuffix(fileInfo.suffix())) return FilePreviewKind::Blocked;

    QMimeDatabase database;
    const auto contentMime = fileInfo.exists()
        ? database.mimeTypeForFile(localPath, QMimeDatabase::MatchContent).name() : QString{};
    const auto extensionMime = database.mimeTypeForFile(localPath, QMimeDatabase::MatchExtension).name();
    // 内容探测优先于服务端声明；octet-stream 信息不足时才回退到声明和扩展名。
    auto resolvedMime = contentMime;
    if (resolvedMime.isEmpty() || resolvedMime == QStringLiteral("application/octet-stream"))
        resolvedMime = mediaType.trimmed().toLower();
    if (resolvedMime.isEmpty() || resolvedMime == QStringLiteral("application/octet-stream"))
        resolvedMime = extensionMime;

    if (resolvedMime.startsWith(QStringLiteral("image/"))) return FilePreviewKind::Image;
    if (resolvedMime.startsWith(QStringLiteral("audio/"))) return FilePreviewKind::Audio;
    if (resolvedMime.startsWith(QStringLiteral("video/"))) return FilePreviewKind::Video;
    return FilePreviewKind::External;
}

FilePreviewDialog::FilePreviewDialog(
    const QString& localPath, const QString& displayName,
    const QString& mediaType, QWidget* parent)
    : QDialog(parent), localPath_(QFileInfo(localPath).absoluteFilePath()),
      kind_(previewKind(localPath_, mediaType))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("文件预览 - %1").arg(displayName));
    setMinimumSize(720, 520);
    resize(920, 680);
    setStyleSheet(QStringLiteral(R"QSS(
QDialog { background:#f5f7fb; }
QLabel#previewSurface { background:#101828; border:1px solid #dce3ee; border-radius:10px; color:#ffffff; }
QLabel#previewStatus { color:#667085; }
)QSS"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);
    auto* title = new QLabel(displayName, this);
    title->setObjectName(QStringLiteral("filePreviewTitle"));
    title->setStyleSheet(QStringLiteral("font-size:18px;font-weight:700;"));
    title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(title);

    statusLabel_ = new QLabel(QStringLiteral("正在准备预览…"), this);
    statusLabel_->setObjectName(QStringLiteral("previewStatus"));

    if (kind_ == FilePreviewKind::Image)
    {
        QImageReader reader(localPath_);
        reader.setAutoTransform(true);
        reader.setDecideFormatFromContent(true);
        const auto image = reader.read();
        if (image.isNull())
        {
            diagnostic_ = QStringLiteral("图片内容无法解码，文件仍已安全保存。");
            return;
        }
        sourceImage_ = QPixmap::fromImage(image);
        imageLabel_ = new QLabel(this);
        imageLabel_->setObjectName(QStringLiteral("previewSurface"));
        imageLabel_->setAlignment(Qt::AlignCenter);
        imageLabel_->setMinimumSize(480, 340);
        root->addWidget(imageLabel_, 1);
        statusLabel_->setText(QStringLiteral("图片预览 · %1 × %2")
            .arg(sourceImage_.width()).arg(sourceImage_.height()));
        updateImagePixmap();
        ready_ = true;
    }
    else if (kind_ == FilePreviewKind::Audio || kind_ == FilePreviewKind::Video)
    {
        player_ = new QMediaPlayer(this);
        audioOutput_ = new QAudioOutput(this);
        audioOutput_->setVolume(0.75F);
        player_->setAudioOutput(audioOutput_);

        if (kind_ == FilePreviewKind::Video)
        {
            videoWidget_ = new QVideoWidget(this);
            videoWidget_->setObjectName(QStringLiteral("previewSurface"));
            videoWidget_->setMinimumSize(560, 360);
            videoWidget_->setAspectRatioMode(Qt::KeepAspectRatio);
            player_->setVideoOutput(videoWidget_);
            root->addWidget(videoWidget_, 1);
        }
        else
        {
            auto* audioArtwork = new QLabel(this);
            audioArtwork->setObjectName(QStringLiteral("previewSurface"));
            audioArtwork->setAlignment(Qt::AlignCenter);
            audioArtwork->setMinimumHeight(300);
            audioArtwork->setPixmap(makeUiIcon(UiIcon::Audio, QColor(QStringLiteral("#7db4ff")))
                .pixmap(112, 112));
            root->addWidget(audioArtwork, 1);
        }

        auto* controls = new QHBoxLayout;
        playButton_ = new QPushButton(QStringLiteral("播放"), this);
        playButton_->setObjectName(QStringLiteral("mediaPlayButton"));
        applyUiIcon(playButton_, UiIcon::Video, 18);
        positionSlider_ = new QSlider(Qt::Horizontal, this);
        positionSlider_->setObjectName(QStringLiteral("mediaPositionSlider"));
        positionSlider_->setRange(0, 1000);
        timeLabel_ = new QLabel(QStringLiteral("0:00 / 0:00"), this);
        volumeSlider_ = new QSlider(Qt::Horizontal, this);
        volumeSlider_->setObjectName(QStringLiteral("mediaVolumeSlider"));
        volumeSlider_->setRange(0, 100);
        volumeSlider_->setValue(75);
        volumeSlider_->setMaximumWidth(110);
        controls->addWidget(playButton_);
        controls->addWidget(positionSlider_, 1);
        controls->addWidget(timeLabel_);
        controls->addWidget(new QLabel(QStringLiteral("音量"), this));
        controls->addWidget(volumeSlider_);
        root->addLayout(controls);

        connect(playButton_, &QPushButton::clicked, this, &FilePreviewDialog::togglePlayback);
        connect(positionSlider_, &QSlider::sliderMoved, this, [this](int value) {
            if (player_ != nullptr && player_->duration() > 0)
                player_->setPosition(player_->duration() * value / 1000);
        });
        connect(volumeSlider_, &QSlider::valueChanged, this, [this](int value) {
            if (audioOutput_ != nullptr) audioOutput_->setVolume(value / 100.0F);
        });
        connect(player_, &QMediaPlayer::positionChanged,
                this, &FilePreviewDialog::updatePlaybackPosition);
        connect(player_, &QMediaPlayer::durationChanged,
                this, [this](qint64) { updatePlaybackPosition(player_->position()); });
        connect(player_, &QMediaPlayer::playbackStateChanged, this,
                [this](QMediaPlayer::PlaybackState state) {
            playButton_->setText(state == QMediaPlayer::PlayingState
                ? QStringLiteral("暂停") : QStringLiteral("播放"));
        });
        connect(player_, &QMediaPlayer::mediaStatusChanged, this,
                [this](QMediaPlayer::MediaStatus status) {
            if (status == QMediaPlayer::LoadedMedia)
                statusLabel_->setText(QStringLiteral("媒体已就绪，点击播放开始预览。"));
            else if (status == QMediaPlayer::InvalidMedia)
                statusLabel_->setText(QStringLiteral("当前媒体格式或编码暂不支持。"));
        });
        connect(player_, &QMediaPlayer::errorOccurred, this,
                [this](QMediaPlayer::Error, const QString&) {
            // 编解码器内部错误可能包含本地路径，界面只展示固定脱敏提示。
            statusLabel_->setText(QStringLiteral("媒体预览失败，可尝试使用系统默认应用打开。"));
        });
        player_->setSource(QUrl::fromLocalFile(localPath_));
        ready_ = true;
    }
    else
    {
        diagnostic_ = kind_ == FilePreviewKind::Blocked
            ? QStringLiteral("安全策略禁止打开可执行文件或脚本。")
            : QStringLiteral("该文件类型应由系统默认应用打开。");
        return;
    }

    root->addWidget(statusLabel_);
    auto* footer = new QHBoxLayout;
    auto* revealButton = new QPushButton(QStringLiteral("打开所在文件夹"), this);
    revealButton->setObjectName(QStringLiteral("previewRevealButton"));
    applyUiIcon(revealButton, UiIcon::Folder, 18);
    auto* defaultButton = new QPushButton(QStringLiteral("使用默认应用打开"), this);
    defaultButton->setObjectName(QStringLiteral("previewDefaultOpenButton"));
    applyUiIcon(defaultButton, UiIcon::File, 18);
    auto* closeButton = new QPushButton(QStringLiteral("关闭"), this);
    connect(revealButton, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(localPath_).absolutePath()));
    });
    connect(defaultButton, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(localPath_));
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
    footer->addWidget(revealButton);
    footer->addWidget(defaultButton);
    footer->addStretch();
    footer->addWidget(closeButton);
    root->addLayout(footer);
}

void FilePreviewDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    updateImagePixmap();
}

void FilePreviewDialog::togglePlayback()
{
    if (player_ == nullptr) return;
    if (player_->playbackState() == QMediaPlayer::PlayingState) player_->pause();
    else player_->play();
}

void FilePreviewDialog::updatePlaybackPosition(qint64 positionMs)
{
    if (player_ == nullptr || positionSlider_ == nullptr || timeLabel_ == nullptr) return;
    const auto duration = player_->duration();
    if (!positionSlider_->isSliderDown())
        positionSlider_->setValue(duration > 0 ? static_cast<int>(positionMs * 1000 / duration) : 0);
    timeLabel_->setText(QStringLiteral("%1 / %2").arg(playbackTime(positionMs), playbackTime(duration)));
}

void FilePreviewDialog::updateImagePixmap()
{
    if (imageLabel_ == nullptr || sourceImage_.isNull()) return;
    const auto target = imageLabel_->size() - QSize(24, 24);
    if (target.width() <= 0 || target.height() <= 0) return;
    imageLabel_->setPixmap(sourceImage_.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

} // namespace orglink::client
