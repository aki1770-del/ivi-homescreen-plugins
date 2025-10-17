/*
 * Copyright 2023, the Chromium project authors.  Please see the AUTHORS file
 * for details. All rights reserved. Use of this source code is governed by a
 * BSD-style license that can be found in the LICENSE file.
 * Copyright 2023, Toyota Connected North America
 */

#ifndef FLUTTER_PLUGIN_CLOUD_FIRESTORE_PLUGIN_C_API_H_
#define FLUTTER_PLUGIN_CLOUD_FIRESTORE_PLUGIN_C_API_H_

#include "flutter_homescreen_plugin.h"

#ifdef FLUTTER_PLUGIN_IMPL
#define FLUTTER_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define FLUTTER_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif



FLUTTER_PLUGIN_EXPORT void CloudFirestorePluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar);

extern FLUTTER_PLUGIN_EXPORT FlutterPlugin CloudFirestorePlugin;



#endif  // FLUTTER_PLUGIN_CLOUD_FIRESTORE_PLUGIN_C_API_H_
