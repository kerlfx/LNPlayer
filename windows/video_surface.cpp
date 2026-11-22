#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "video_surface.h"
#include "d3d11_video_renderer.h"

#include <Windows.h>
#include <CommCtrl.h>

#include <QMouseEvent>
#include <QContextMenuEvent>
#include <cstdlib>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QApplication>
#include <QCursor>

#include <algorithm>
#include <utility>
#include <vector>

namespace
{
constexpr int kWindowResizeBorder = 6;

LRESULT CALLBACK video_surface_subclass_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR subclass_id, DWORD_PTR reference_data)
{
    auto* surface = reinterpret_cast<VideoSurface*>(reference_data);
    if (message == WM_NCHITTEST && surface)
    {
        QWidget* top_level = surface->window();
        if (top_level && !top_level->isMaximized() && !top_level->isFullScreen())
        {
            const QPoint position = top_level->mapFromGlobal(QCursor::pos());
            const bool on_edge = position.x() < kWindowResizeBorder ||
                position.y() < kWindowResizeBorder ||
                position.x() >= top_level->width() - kWindowResizeBorder ||
                position.y() >= top_level->height() - kWindowResizeBorder;
            if (on_edge)
                return HTTRANSPARENT;
        }
    }

    if (message == WM_NCDESTROY)
    {
        RemoveWindowSubclass(window, video_surface_subclass_proc, subclass_id);
        return DefSubclassProc(window, message, wparam, lparam);
    }
    return DefSubclassProc(window, message, wparam, lparam);
}
}

VideoSurface::VideoSurface(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(160, 90);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    setStyleSheet(QStringLiteral("background-color: #101010;"));

    // Resizing a flip/discard swap chain invalidates its back buffer. Coalesce
    // the stream of QWidget resize events and redraw the cached frame once the
    // window geometry has settled.
    resize_timer_.setSingleShot(true);
    resize_timer_.setInterval(60);
    connect(&resize_timer_, &QTimer::timeout, this, [this]
    {
        if (!renderer_ || pending_renderer_size_.isEmpty())
            return;

        renderer_->resize(
            static_cast<uint32_t>(pending_renderer_size_.width()),
            static_cast<uint32_t>(pending_renderer_size_.height()));
        pending_renderer_size_ = {};

        QImage frame;
        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            frame = frame_;
        }
        if (!frame.isNull())
            renderer_->present(
                frame.constBits(), frame.width(), frame.height(), frame.bytesPerLine());
    });

    renderer_ = new lnplayer::windows::D3D11VideoRenderer();
    const HWND window = reinterpret_cast<HWND>(static_cast<quintptr>(winId()));
    SetWindowSubclass(
        window, video_surface_subclass_proc,
        reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this));
    if (!renderer_->initialize(window))
    {
        delete renderer_;
        renderer_ = nullptr;
    }
}

VideoSurface::~VideoSurface()
{
    qInfo() << "[Shutdown] VideoSurface destructor entered:" << this
            << "renderer:" << renderer_
            << "children:" << children().size();
    const HWND window = reinterpret_cast<HWND>(static_cast<quintptr>(winId()));
    RemoveWindowSubclass(
        window, video_surface_subclass_proc, reinterpret_cast<UINT_PTR>(this));
    qInfo() << "[Shutdown] VideoSurface renderer destruction begin:" << renderer_;
    delete renderer_;
    renderer_ = nullptr;
    qInfo() << "[Shutdown] VideoSurface renderer destruction complete:" << this;
}

void VideoSurface::set_frame(const QImage& frame)
{
    const QImage frame_copy = frame;
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        frame_ = frame_copy;
    }

    if (renderer_)
    {
        if (frame_copy.isNull())
            renderer_->clear();
        else
            renderer_->present(
                frame_copy.constBits(),
                frame_copy.width(),
                frame_copy.height(),
                frame_copy.bytesPerLine());
        return;
    }
    update();
}

bool VideoSurface::has_renderer() const
{
    return renderer_ != nullptr;
}

void* VideoSurface::native_device() const
{
    return renderer_ ? renderer_->native_device() : nullptr;
}

void VideoSurface::set_gesture_enabled(bool enabled)
{
    gesture_enabled_ = enabled;
    gesture_triggered_ = false;
}

bool VideoSurface::consume_gesture_context_menu()
{
    const bool triggered = gesture_triggered_;
    gesture_triggered_ = false;
    return triggered;
}

void VideoSurface::set_stretch(bool enabled)
{
    stretch_ = enabled;
    if (renderer_)
        renderer_->set_stretch(enabled);
    else
        update();
}

void VideoSurface::set_aspect_ratio(float ratio)
{
    aspect_ratio_ = ratio;
    stretch_ = false;
    if (renderer_)
        renderer_->set_aspect_ratio(ratio > 0.0f ? ratio : source_aspect_ratio_);
    else
        update();
}

void VideoSurface::adjust_zoom(int direction)
{
    zoom_ = std::clamp(zoom_ + static_cast<float>(direction) * 0.1f, 1.0f, 3.0f);
    if (renderer_)
        renderer_->set_zoom(zoom_);
    else
        update();
}

float VideoSurface::zoom() const
{
    return zoom_;
}

QRect VideoSurface::display_rect() const
{
    if (width() <= 0 || height() <= 0)
        return {};

    float display_aspect = aspect_ratio_ > 0.0f
        ? aspect_ratio_ : source_aspect_ratio_;
    if (display_aspect <= 0.0f)
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        if (!frame_.isNull())
            display_aspect = frame_.width() / static_cast<float>(frame_.height());
    }
    if (display_aspect <= 0.0f || stretch_)
        return rect();

    const QSize fitted = QSize(
        qRound(height() * display_aspect), height()).scaled(
            size(), Qt::KeepAspectRatio);
    const QSize zoomed(
        qRound(fitted.width() * zoom_),
        qRound(fitted.height() * zoom_));
    return QRect(
        (width() - zoomed.width()) / 2,
        (height() - zoomed.height()) / 2,
        zoomed.width(),
        zoomed.height()).intersected(rect());
}

void VideoSurface::set_source_aspect_ratio(float ratio)
{
    source_aspect_ratio_ = ratio;
    if (aspect_ratio_ <= 0.0f && renderer_)
        renderer_->set_aspect_ratio(source_aspect_ratio_);
    else
        update();
}

void VideoSurface::set_video_filter(
    float brightness, float contrast, float saturation, float hue_degrees,
    float sharpness, float denoise, bool grayscale)
{
    if (renderer_)
        renderer_->set_video_filter(
            brightness, contrast, saturation, hue_degrees, sharpness, denoise, grayscale);
}

void VideoSurface::set_hdr_metadata(int transfer, int color_primaries, float peak_luminance)
{
    if (renderer_)
        renderer_->set_hdr_metadata(transfer, color_primaries, peak_luminance);
}

void VideoSurface::set_video_color_space(int ycbcr_matrix, int full_range)
{
    if (renderer_)
        renderer_->set_video_color_space(ycbcr_matrix, full_range);
}

bool VideoSurface::present_frame_threadsafe(const QImage& frame)
{
    if (!renderer_ || frame.isNull())
        return false;

    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        frame_ = frame;
    }
    return renderer_->present(
        frame.constBits(),
        frame.width(),
        frame.height(),
        frame.bytesPerLine());
}

bool VideoSurface::present_hardware_frame(
    void* texture, uint32_t array_slice, uint32_t format,
    int width, int height, std::shared_ptr<void> frame_owner,
    const QImage& overlay)
{
    return renderer_ && renderer_->present_hardware(
        texture, array_slice, format, width, height, std::move(frame_owner),
        overlay.isNull() ? nullptr : overlay.constBits(),
        overlay.isNull() ? 0 : overlay.width(),
        overlay.isNull() ? 0 : overlay.height(),
        overlay.isNull() ? 0 : overlay.bytesPerLine());
}

QImage VideoSurface::capture_frame() const
{
    // Safe on the worker pool: D3D11VideoRenderer serializes capture and present
    // through its own mutex. MainWindow keeps this widget alive until tracked
    // background tasks have completed.
    const auto fallback_frame = [this]
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        return frame_;
    };

    if (!renderer_)
        return fallback_frame();
    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;
    if (!renderer_->capture_bgra(pixels, width, height))
        return fallback_frame();
    return QImage(
        pixels.data(), width, height, width * 4, QImage::Format_ARGB32).copy();
}

void VideoSurface::set_interaction_callbacks(
    std::function<void()> double_click,
    std::function<void(int, bool)> wheel,
    std::function<void()> middle_click,
    std::function<void(int)> gesture)
{
    double_click_callback_ = std::move(double_click);
    wheel_callback_ = std::move(wheel);
    middle_click_callback_ = std::move(middle_click);
    gesture_callback_ = std::move(gesture);
}

void VideoSurface::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && double_click_callback_)
        double_click_callback_();
    QWidget::mouseDoubleClickEvent(event);
}

void VideoSurface::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        left_press_position_ = event->globalPosition().toPoint();
        QWidget* top_level = window();
        left_press_window_offset_ = top_level
            ? left_press_position_ - top_level->frameGeometry().topLeft()
            : QPoint();
        window_move_started_ = false;
    }
    if (event->button() == Qt::RightButton)
    {
        right_press_position_ = event->pos();
        gesture_triggered_ = false;
    }
    if (event->button() == Qt::MiddleButton && middle_click_callback_)
    {
        middle_click_callback_();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void VideoSurface::mouseMoveEvent(QMouseEvent* event)
{
    QWidget* top_level = window();
    if (top_level && !top_level->isMaximized() && !top_level->isFullScreen() &&
        (event->buttons() & Qt::LeftButton))
    {
        const QPoint global_position = event->globalPosition().toPoint();
        if (window_move_started_ ||
            (global_position - left_press_position_).manhattanLength() >=
                QApplication::startDragDistance())
        {
            window_move_started_ = true;
            top_level->move(global_position - left_press_window_offset_);
            event->accept();
            return;
        }
    }
    if (gesture_enabled_ && !gesture_triggered_ &&
        (event->buttons() & Qt::RightButton))
    {
        const QPoint delta = event->pos() - right_press_position_;
        if (std::abs(delta.x()) >= 36 || std::abs(delta.y()) >= 36)
        {
            if (gesture_callback_)
            {
                const int direction = std::abs(delta.x()) >= std::abs(delta.y())
                    ? (delta.x() < 0 ? 0 : 1)
                    : (delta.y() < 0 ? 2 : 3);
                gesture_triggered_ = true;
                gesture_callback_(direction);
                event->accept();
                return;
            }
        }
    }
    QWidget::mouseMoveEvent(event);
}

void VideoSurface::contextMenuEvent(QContextMenuEvent* event)
{
    if (gesture_triggered_)
    {
        gesture_triggered_ = false;
        event->accept();
        return;
    }
    QWidget::contextMenuEvent(event);
}

void VideoSurface::wheelEvent(QWheelEvent* event)
{
    if (wheel_callback_ && event->angleDelta().y() != 0)
        wheel_callback_(
            event->angleDelta().y() > 0 ? 1 : -1,
            event->modifiers().testFlag(Qt::ControlModifier));
    event->accept();
}

void VideoSurface::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (renderer_)
        return;

    QImage frame;
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        frame = frame_;
    }

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (frame.isNull())
        return;

    const float display_aspect = aspect_ratio_ > 0.0f
        ? aspect_ratio_
        : source_aspect_ratio_;
    const QSize source_size = display_aspect > 0.0f
        ? QSize(static_cast<int>(frame.height() * display_aspect), frame.height())
        : frame.size();
    const QSize fitted = source_size.scaled(
        size(), stretch_ ? Qt::IgnoreAspectRatio : Qt::KeepAspectRatio);
    const QSize zoomed(
        static_cast<int>(fitted.width() * zoom_),
        static_cast<int>(fitted.height() * zoom_));
    const QRect target(
        (width() - zoomed.width()) / 2,
        (height() - zoomed.height()) / 2,
        zoomed.width(),
        zoomed.height());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(target, frame);
}

void VideoSurface::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (renderer_)
    {
        pending_renderer_size_ = event->size();
        resize_timer_.start();
    }
}

