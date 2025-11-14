# file_selector

This plugin is used with the pub.dev package `file_selector`
https://pub.dev/packages/file_selector

### Notes

* Depends on zenity being present on a system
* mimeTypes are not currently supported

## Functional Test Case

https://github.com/flutter/packages/tree/main/packages/file_selector/file_selector/example

## Update Pigeons C++

```
git clone https://github.com/flutter/packages
cd packages/file_selector/file_selector_linux
flutter pub run pigeon --input pigeons/messages.dart --cpp_header_out messages.g.h --cpp_source_out messages.g.cc --cpp_namespace plugin_file_selector
```

copy C++ source files to ivi-homescreen-plugins, fix linter errors, confirm interface is overridden and implemented.