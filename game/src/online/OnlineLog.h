//
// Licensed under the MIT license. See LICENSE file in the project root for details.
//

#pragma once

#include <string>

// Diagnostic log of the online mode (HadesCoopOnline.log next to the mod library).
// Players can attach it to bug reports. Safe to use from any thread.
namespace OnlineLog {

void Open(const std::wstring &path);
void Write(const char *format, ...);

} // namespace OnlineLog
