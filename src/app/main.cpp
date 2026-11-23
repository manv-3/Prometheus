#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QString>
#include <QMetaObject>

#include "prometheus/ui/OrganismViewModel.hpp"
#include "prometheus/core/RuntimeCoordinator.hpp"
#include "prometheus/common/ComputeBudget.hpp"

#include <optional>

Q_DECLARE_METATYPE(std::shared_ptr<const prometheus::common::OrganismSnapshot>)

namespace {

/// Parse --fixed-tier N from argv.
/// Valid values: 0 (Dormant) through 4 (Optimal).
/// Returns nullopt if the flag is absent or the value is invalid.
std::optional<prometheus::common::QualityTier> parseFixedTier(const QStringList& args) {
    int idx = args.indexOf(QStringLiteral("--fixed-tier"));
    if (idx < 0 || idx + 1 >= args.size()) return std::nullopt;

    bool ok = false;
    int val = args[idx + 1].toInt(&ok);
    if (!ok || val < 0 || val > 4) return std::nullopt;

    return static_cast<prometheus::common::QualityTier>(val);
}

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("PROMETHEUS");
    app.setApplicationVersion("0.1.0");

    const QStringList args = app.arguments();

    // --synthetic  : use scripted telemetry (REQ-SENSE-03)
    const bool syntheticMode = args.contains(QStringLiteral("--synthetic"));

    // --fixed-tier N : pin Metabolism to tier N — disables adaptive behaviour
    //                  Used for the fixed-complexity benchmark baseline (TASK-4.2)
    const auto fixedTier = parseFixedTier(args);

    if (fixedTier.has_value()) {
        // Log this clearly so benchmark output cannot be confused with adaptive runs
        qInfo("[PROMETHEUS] Fixed-complexity mode: tier %d (adaptive behaviour disabled)",
              static_cast<int>(*fixedTier));
    }

    qRegisterMetaType<std::shared_ptr<const prometheus::common::OrganismSnapshot>>();

    // ---- ViewModel ----
    prometheus::ui::OrganismViewModel viewModel;

    // ---- Runtime Coordinator ----
    prometheus::core::RuntimeCoordinator coordinator;

    coordinator.setSnapshotCallback(
        [&viewModel](std::shared_ptr<const prometheus::common::OrganismSnapshot> snap) {
            QMetaObject::invokeMethod(&viewModel, "onSnapshotPublished",
                Qt::QueuedConnection,
                Q_ARG(std::shared_ptr<const prometheus::common::OrganismSnapshot>,
                      std::move(snap)));
        }
    );

    coordinator.start(syntheticMode, fixedTier);

    // ---- QML engine ----
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("vm", &viewModel);
    engine.rootContext()->setContextProperty("syntheticModeEnabled", syntheticMode);
    engine.rootContext()->setContextProperty("fixedTierMode", fixedTier.has_value());

    const QUrl mainQml(QStringLiteral("qrc:/Prometheus/resources/qml/Main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [](const QUrl& url) {
            qCritical("[PROMETHEUS] Failed to load QML component: %s", qUtf8Printable(url.toString()));
            QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection
    );
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::warnings,
        [](const QList<QQmlError>& warnings) {
            for (const auto& w : warnings) {
                qWarning("[PROMETHEUS QML] %s", qUtf8Printable(w.toString()));
            }
        }
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
