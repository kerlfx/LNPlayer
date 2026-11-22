#include "main_window.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("LNPlayer"));
    application.setApplicationName(QStringLiteral("LNPlayer"));
    application.setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.addHelpOption();

    QCommandLineOption audio_option(QStringList{QStringLiteral("a"), QStringLiteral("audio")},
        QStringLiteral("启动音频界面"));
    QCommandLineOption video_option(QStringList{QStringLiteral("v"), QStringLiteral("video")},
        QStringLiteral("启动视频界面"));
    parser.addOption(audio_option);
    parser.addOption(video_option);
    parser.addPositionalArgument(
        QStringLiteral("media"),
        QStringLiteral("要打开的媒体文件"),
        QStringLiteral("[media...]"));

    parser.process(application);

    const PlayerMode mode = parser.isSet(audio_option)
        ? PlayerMode::Audio
        : PlayerMode::Video;

    const QStringList initial_media_files = parser.positionalArguments();

    int exit_code = 0;
    {
        MainWindow window(mode, initial_media_files);
        window.show();

        exit_code = application.exec();
        qInfo() << "[Shutdown] QApplication::exec returned:" << exit_code;
    }

    qInfo() << "[Shutdown] MainWindow destruction completed";
    return exit_code;
}
