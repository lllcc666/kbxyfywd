#pragma once
// 兼容入口：历史上各模块都 include 这个头。
// 新代码请按职责直接 include 下面四个窄头，避免一个模块改动导致全项目重编译。
#include "window_messages.h"
#include "hook_lifecycle.h"
#include "packet_io.h"
#include "packet_hijack.h"
#include "activity_actions.h"
