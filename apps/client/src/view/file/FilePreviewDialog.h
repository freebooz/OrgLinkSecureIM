#pragma once

#include <QDialog>
#include <QPixmap>

class QLabel;
class QPushButton;
class QResizeEvent;
class QSlider;
class QMediaPlayer;
class QAudioOutput;
class QVideoWidget;

namespace orglink::client
{

/** @brief 下载文件的本地打开策略；危险类型永远不会交给系统 Shell 执行。 */
enum class FilePreviewKind
{
    Image,
    Audio,
    Video,
    External,
    Blocked
};

/**
 * @brief 图片、音频和视频的应用内预览窗口。
 *
 * 本窗口只读取 Controller 已原子落盘且通过服务端完整性校验的本地文件，不访问网络、数据库或
 * MinIO。图片由 QImageReader 解码，音视频由与客户端同版本的 Qt Multimedia 后端解码；
 * 不支持的普通文档由 Controller 交给系统默认应用，脚本和可执行文件始终阻止打开。
 */
class FilePreviewDialog final : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief 构造非模态预览窗口。
     * @param localPath 已下载文件的绝对路径，生命周期独立于窗口。
     * @param displayName 可信展示文件名，仅作为纯文本标题使用。
     * @param mediaType 服务端记录的 MIME 类型；最终分类还会结合本地内容探测和扩展名。
     * @param parent 所属主窗口；主窗口关闭时预览窗口同步销毁。
     */
    FilePreviewDialog(const QString& localPath, const QString& displayName,
                      const QString& mediaType, QWidget* parent = nullptr);

    /**
     * @brief 根据危险扩展名、内容 MIME 和声明 MIME 决定预览策略。
     * @return 图片/音频/视频使用应用内预览，普通文档使用 External，危险类型返回 Blocked。
     */
    [[nodiscard]] static FilePreviewKind previewKind(
        const QString& localPath, const QString& mediaType = {});

    /** @brief 返回预览控件是否成功初始化；图片解码失败时为 false。 */
    [[nodiscard]] bool isReady() const noexcept { return ready_; }
    /** @brief 返回不含内部路径和编解码器细节的用户提示。 */
    [[nodiscard]] QString diagnostic() const { return diagnostic_; }

protected:
    /** @brief 图片窗口缩放时按原始比例重绘，不修改原文件或缓存。 */
    void resizeEvent(QResizeEvent* event) override;

private:
    /** @brief 播放/暂停当前媒体；无媒体或加载失败时不改变状态。 */
    void togglePlayback();
    /** @brief 根据播放器位置刷新进度和时长文本；时间单位为毫秒。 */
    void updatePlaybackPosition(qint64 positionMs);
    /** @brief 将原始图片缩放到当前预览区域，避免大图撑破窗口布局。 */
    void updateImagePixmap();

    /** @brief 已下载文件绝对路径；只读，不在析构时删除。 */
    QString localPath_;
    /** @brief 解析后的预览类型；构造后保持不变。 */
    FilePreviewKind kind_{FilePreviewKind::External};
    /** @brief 初始化结果与脱敏错误；失败窗口不会展示。 */
    bool ready_{false};
    QString diagnostic_;
    /** @brief 图片原始像素和显示标签；仅 Image 类型创建有效内容。 */
    QPixmap sourceImage_;
    QLabel* imageLabel_{nullptr};
    /** @brief 媒体播放状态、时长和交互控件；仅 Audio/Video 类型使用。 */
    QLabel* statusLabel_{nullptr};
    QLabel* timeLabel_{nullptr};
    QPushButton* playButton_{nullptr};
    QSlider* positionSlider_{nullptr};
    QSlider* volumeSlider_{nullptr};
    QMediaPlayer* player_{nullptr};
    QAudioOutput* audioOutput_{nullptr};
    QVideoWidget* videoWidget_{nullptr};
};

} // namespace orglink::client
