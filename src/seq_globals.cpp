/*
 * seq_globals.cpp — definitions of the cross-cutting globals that the
 * extracted showeq code references via `main.h`.
 *
 * In the legacy showeq/src/main.cpp (a 500+ line QApplication bootstrap
 * that also wired up every Qt widget), these were defined and populated
 * from XML preferences before the main window was built. The daemon
 * doesn't have a main window, so we define them here and let the normal
 * showeq_params defaults apply. A richer bootstrap (loading showeq.xml,
 * command-line overrides, etc.) is a Phase 1 follow-up; nothing on the
 * critical path for getting the daemon to build needs it.
 */

#include "main.h"
#include "tomlpreferences.h"

struct ShowEQParams* showeq_params = nullptr;
TomlPreferences*     pSEQPrefs     = nullptr;

namespace seq {
// Called from DaemonApp before any extracted classes are constructed.
void initGlobals(const QString& configDef, const QString& configFile)
{
    pSEQPrefs     = new TomlPreferences(configDef, configFile);
    showeq_params = new ShowEQParams();
    // Fields are zero/false by default; opt-ins below match showeq's
    // usual "sensible runtime" settings rather than its XML defaults.
    showeq_params->createUnknownSpawns   = true;
    showeq_params->keep_selected_visible = true;
    showeq_params->saveZoneState         = false;
    showeq_params->savePlayerState       = false;
    showeq_params->saveSpawns            = false;
}
} // namespace seq
