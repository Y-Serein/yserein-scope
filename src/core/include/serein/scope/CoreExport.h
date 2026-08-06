#pragma once

#include <QtCore/QtGlobal>

#if defined(SEREIN_SCOPE_CORE_LIBRARY)
#  define SEREIN_SCOPE_CORE_EXPORT Q_DECL_EXPORT
#else
#  define SEREIN_SCOPE_CORE_EXPORT Q_DECL_IMPORT
#endif
