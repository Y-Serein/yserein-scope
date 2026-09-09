#pragma once

#include <serein/scope/WorkspaceCanvas.h>

// Source-compatibility bridge for existing TN160 NCM pages. New components
// should include the Core header and use serein::scope::WorkspaceCanvas.
namespace serein::scope::tn160ncm {

using ComponentFrame = ::serein::scope::ComponentFrame;
using WorkspaceCanvas = ::serein::scope::WorkspaceCanvas;

} // namespace serein::scope::tn160ncm
