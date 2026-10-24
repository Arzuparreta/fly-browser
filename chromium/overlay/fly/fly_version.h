// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_FLY_VERSION_H_
#define FLY_FLY_VERSION_H_

// Fly's product version. scripts/version-sync.js copies it from package.json;
// `npm test` fails if it drifts. The engine version is separate
// (version_info::GetVersionNumber()).
inline constexpr char kFlyVersion[] = "0.30.1";

#endif  // FLY_FLY_VERSION_H_
