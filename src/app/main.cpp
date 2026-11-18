#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QString>

#include "prometheus/ui/OrganismViewModel.hpp"
#include "prometheus/core/SimulationThread.hpp"
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

    // ---- Simulation thread ----
    // Publishes snapshots via a queued connection to the ViewModel.
    // The render clock is completely independent (TASK-1.8 / REQ-BODY-02).
    prometheus::core::SimulationThread simThread(
        [&viewModel](std::shared_ptr<const prometheus::common::OrganismSnapshot> snap) {
            // QMetaObject::invokeMethod ensures the slot runs on the GUI thread
            // even though the lambda executes on the simulation thread.
            QMetaObject::invokeMethod(&viewModel, "onSnapshotPublished",
                Qt::QueuedConnection,
                Q_ARG(std::shared_ptr<const prometheus::common::OrganismSnapshot>,
                      std::move(snap)));
        }
    );

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
        return -1;
    }

    // ---- Start simulation (independent of render clock) ----
    simThread.start();

    const int result = app.exec();

    simThread.stop();
    return result;
}
