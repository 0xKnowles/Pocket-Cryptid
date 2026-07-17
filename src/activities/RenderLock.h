#pragma once

class Activity;  // forward declaration

// RAII helper to lock the ActivityManager's rendering mutex for the duration of a scope.
// Implementation lives in ActivityManager.cpp alongside the mutex it guards.
class RenderLock {
  bool isLocked = false;

 public:
  explicit RenderLock();
  explicit RenderLock(Activity&);  // unused for now, kept for call-site symmetry
  RenderLock(const RenderLock&) = delete;
  RenderLock& operator=(const RenderLock&) = delete;
  ~RenderLock();
  void unlock();
  static bool peek();
};
