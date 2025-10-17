/*
 * Copyright 2023, the Chromium project authors.  Please see the AUTHORS file
 * for details. All rights reserved. Use of this source code is governed by a
 * BSD-style license that can be found in the LICENSE file.
 * Copyright 2023, Toyota Connected North America
 */

#ifndef FLUTTER_PLUGIN_FIREBASE_STORAGE_PLUGIN_C_API_H
#define FLUTTER_PLUGIN_FIREBASE_STORAGE_PLUGIN_C_API_H

#include "flutter_homescreen_plugin.h"

#ifdef FLUTTER_PLUGIN_IMPL
#define FLUTTER_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define FLUTTER_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif



FLUTTER_PLUGIN_EXPORT void FirebaseStoragePluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar);

extern FLUTTER_PLUGIN_EXPORT FlutterPlugin FirebaseStoragePlugin;



#endif /* FLUTTER_PLUGIN_FIREBASE_STORAGE_PLUGIN_C_API_H */
