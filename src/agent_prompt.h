#pragma once

#include "replay_config.h"

namespace replay {

enum class AgentPromptTopic { Setup, Resources, Exclusions };

struct AgentPromptContext {
    QString executable;
    ReplayPaths paths;
    QString historyDirectory;
    ReplayConfig shownSettings;
    bool configEditable = true;
};

// Installed-app context only: no source-tree discovery or private screen data.
QString configurationAgentPrompt(AgentPromptTopic topic, const AgentPromptContext& context);

}  // namespace replay
