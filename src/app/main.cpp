#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QString>
#include <QMetaObject>

#include "prometheus/ui/OrganismViewModel.hpp"
#include "prometheus/core/RuntimeCoordinator.hpp"

// Register the snapshot type for queued cross-thread signals.
Q_DECLARE_METATYPE(std::shared_ptr<const prometheus::common::OrganismSnapshot>)

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("PROMETHEUS");
    app.setApplicationVersion("0.1.0");

    // Parse --synthetic flag (REQ-SENSE-03: synthetic telemetry mode)
    const bool syntheticMode = app.arguments().contains(QStringLiteral("--synthetic"));

    // Register cross-thread signal type
    qRegisterMetaType<std::shared_ptr<const prometheus::common::OrganismSnapshot>>();

    // ---- ViewModel (GUI thread owner) ----
    prometheus::ui::OrganismViewModel viewModel;

    // ---- Runtime Coordinator ----
    // Owns the simulation thread, sensor worker, brain, metabolism, body.
    // The render clock is completely independent (TASK-1.8 / REQ-BODY-02).
    prometheus::core::RuntimeCoordinator coordinator;

    // Wire snapshot publishing: simulation thread → (queued) → ViewModel
    coordinator.setSnapshotCallback(
        [&viewModel](std::shared_ptr<const prometheus::common::OrganismSnapshot> snap) {
            // QMetaObject::invokeMethod ensures the slot runs on the GUI thread
            QMetaObject::invokeMethod(&viewModel, "onSnapshotPublished",
                Qt::QueuedConnection,
                Q_ARG(std::shared_ptr<const prometheus::common::OrganismSnapshot>,
                      std::move(snap)));
        }
    );

    // Start the organism (synthetic or real telemetry)
    coordinator.start(syntheticMode);

    // ---- QML engine ----
    QQmlApplicationEngine engine;

    // Expose ViewModel and synthetic flag to QML
    engine.rootContext()->setContextProperty("vm", &viewModel);
    engine.rootContext()->setContextProperty("syntheticModeEnabled", syntheticMode);

    const QUrl mainQml(QStringLiteral("qrc:/Prometheus/resources/qml/Main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection
    );
    engine.load(mainQml);

    if (engine.rootObjects().isEmpty()) {
        coordinator.stop();
        return -1;
    }

    const int result = app.exec();

    coordinator.stop();
    return result;
}
