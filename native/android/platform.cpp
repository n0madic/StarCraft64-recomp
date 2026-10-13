// Android services through SDL's activity and JNI environment: app-private
// storage, bundled ROM assets and the vibrator helper class.
#include "../shell/platform.hpp"
#include "../shell/log.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>
#include <jni.h>
#include <stdexcept>
#include <string>

namespace {
void clear_exception(JNIEnv* env) {
    if (env->ExceptionCheck()) env->ExceptionClear();
}
// Owns a JNI local reference on every exit path.
struct Local {
    JNIEnv* env;
    jobject value;
    Local(JNIEnv* e, jobject v) : env(e), value(v) {}
    Local(const Local&) = delete;
    Local& operator=(const Local&) = delete;
    ~Local() { if (value) env->DeleteLocalRef(value); }
};
// App classes are not visible to FindClass from native threads; resolve them
// through the activity's class loader.
jclass load_class(JNIEnv* env, jobject activity, const char* name) {
    Local activity_class(env, env->GetObjectClass(activity));
    const auto get_loader = env->GetMethodID(static_cast<jclass>(activity_class.value), "getClassLoader",
                                             "()Ljava/lang/ClassLoader;");
    Local loader(env, env->CallObjectMethod(activity, get_loader));
    clear_exception(env);
    if (!loader.value) return nullptr;
    Local loader_class(env, env->FindClass("java/lang/ClassLoader"));
    const auto load = env->GetMethodID(static_cast<jclass>(loader_class.value), "loadClass",
                                       "(Ljava/lang/String;)Ljava/lang/Class;");
    Local java_name(env, env->NewStringUTF(name));
    auto result = static_cast<jclass>(env->CallObjectMethod(loader.value, load, java_name.value));
    clear_exception(env);
    return result;
}
}

std::string sc64_storage_path() {
    const char* path = SDL_GetAndroidInternalStoragePath();
    if (!path) throw std::runtime_error(std::string("Android storage unavailable: ") + SDL_GetError());
    return path;
}

void sc64_platform_vibrate(int milliseconds, int amplitude) {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!env) return;
    Local activity(env, static_cast<jobject>(SDL_GetAndroidActivity()));
    if (!activity.value) return;
    Local haptics(env, load_class(env, activity.value, "com.nomadic.sc64.Haptics"));
    if (!haptics.value) { sc64::shell::log::warning("Android haptics class unavailable"); return; }
    const auto method = env->GetStaticMethodID(static_cast<jclass>(haptics.value), "vibrate",
                                               "(Landroid/app/Activity;II)V");
    clear_exception(env);
    if (!method) return;
    env->CallStaticVoidMethod(static_cast<jclass>(haptics.value), method, activity.value, jint(milliseconds),
                              jint(amplitude));
    clear_exception(env);
}

// APK assets are read straight into owned memory: no extra on-disk ROM copy.
std::vector<std::uint8_t> sc64_read_bundled_rom() {
    return sc64_read_packaged_rom("sc64/sc64-ntsc-u.z64");
}
