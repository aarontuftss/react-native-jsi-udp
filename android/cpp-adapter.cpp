#include <memory>
#include <jni.h>
#include <jsi/jsi.h>
#include "react-native-jsi-udp.h"
#include <android/log.h>
#include <string>

// Keep a global manager pointer
std::shared_ptr<jsiudp::UdpManager> manager;

// Global JavaVM pointer
static JavaVM* g_vm = nullptr;

// Cache important classes and methods as global references
static jclass g_moduleClass = nullptr;
static jmethodID g_emitDeviceEventMethod = nullptr;
static jclass g_argumentsClass = nullptr;
static jmethodID g_createMapMethod = nullptr;

// JNI_OnLoad to store JavaVM* and cache essential classes
jint JNI_OnLoad(JavaVM* vm, void*) {
  g_vm = vm;
  
  // Get JNI environment
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return JNI_VERSION_1_6; // Just return without caching on failure
  }
  
  // Find and cache the JsiUdpModule class - we'll use this to get the classloader
  jclass localModuleClass = env->FindClass("com/jsiudp/JsiUdpModule");
  if (localModuleClass) {
    g_moduleClass = (jclass)env->NewGlobalRef(localModuleClass);
    env->DeleteLocalRef(localModuleClass);
    __android_log_print(ANDROID_LOG_INFO, "JsiUdp", "Successfully cached JsiUdpModule class");
  } else {
    __android_log_print(ANDROID_LOG_ERROR, "JsiUdp", "Failed to find JsiUdpModule class");
    env->ExceptionClear(); // Clear any exception that might have occurred
  }
  
  return JNI_VERSION_1_6;
}

// Clean up global references when the library is unloaded
void JNI_OnUnload(JavaVM* vm, void* reserved) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
    if (g_moduleClass) {
      env->DeleteGlobalRef(g_moduleClass);
      g_moduleClass = nullptr;
    }
    if (g_argumentsClass) {
      env->DeleteGlobalRef(g_argumentsClass);
      g_argumentsClass = nullptr;
    }
  }
}

// Helper to get a class using the right classloader
jclass FindClass(JNIEnv* env, const char* className) {
  if (!g_moduleClass) {
    __android_log_print(ANDROID_LOG_ERROR, "JsiUdp", "Module class not available for classloader");
    return nullptr;
  }
  
  // Get the class's ClassLoader
  jmethodID getClassLoader = env->GetMethodID(env->GetObjectClass(g_moduleClass), 
                                             "getClassLoader", 
                                             "()Ljava/lang/ClassLoader;");
  if (!getClassLoader) {
    __android_log_print(ANDROID_LOG_ERROR, "JsiUdp", "Could not find getClassLoader method");
    return nullptr;
  }
  
  jobject classLoader = env->CallObjectMethod(g_moduleClass, getClassLoader);
  if (!classLoader) {
    __android_log_print(ANDROID_LOG_ERROR, "JsiUdp", "Could not get ClassLoader instance");
    return nullptr;
  }
  
  // Find loadClass method
  jmethodID loadClass = env->GetMethodID(env->GetObjectClass(classLoader), 
                                       "loadClass", 
                                       "(Ljava/lang/String;)Ljava/lang/Class;");
  if (!loadClass) {
    __android_log_print(ANDROID_LOG_ERROR, "JsiUdp", "Could not find loadClass method");
    env->DeleteLocalRef(classLoader);
    return nullptr;
  }
  
  // Load the class
  jstring classNameStr = env->NewStringUTF(className);
  jclass result = (jclass)env->CallObjectMethod(classLoader, loadClass, classNameStr);
  
  if (env->ExceptionCheck()) {
    __android_log_print(ANDROID_LOG_ERROR, "JsiUdp", "Exception while loading class %s", className);
    env->ExceptionClear();
    result = nullptr;
  }
  
  env->DeleteLocalRef(classNameStr);
  env->DeleteLocalRef(classLoader);
  
  return result;
}

// Helper to emit events to JS via DeviceEventManagerModule
extern "C" void emit_udp_device_event(const char* eventName, const char* eventType,
                                      const void* data, size_t dataLength,
                                      const char* family, const char* address, int port,
                                      const char* errorMsg, int socketId) {
  if (!g_vm) {
    __android_log_print(ANDROID_LOG_ERROR, "JsiUdp", "JavaVM not initialized");
    return;
  }
  
  JNIEnv* env = nullptr;
  bool didAttach = false;
  jint getEnvStat = g_vm->GetEnv((void**)&env, JNI_VERSION_1_6);
  
  if (getEnvStat == JNI_EDETACHED) {
    JavaVMAttachArgs args;
    args.version = JNI_VERSION_1_6;
    args.name = "UdpNativeThread";
    args.group = nullptr;
    
    if (g_vm->AttachCurrentThread(&env, &args) != 0) {
      __android_log_print(ANDROID_LOG_ERROR, "JsiUdp", "Failed to attach thread");
      return;
    }
    didAttach = true;
  } else if (getEnvStat != JNI_OK) {
    __android_log_print(ANDROID_LOG_ERROR, "JsiUdp", "Failed to get JNIEnv");
    return;
  }

  // Clear any pending exceptions before we start
  if (env->ExceptionCheck()) {
    __android_log_print(ANDROID_LOG_ERROR, "JsiUdp", "Clearing pending exception before event emission");
    env->ExceptionClear();
  }

  // Use cached module class or try to find it
  jclass moduleClass = g_moduleClass;
  if (!moduleClass) {
    moduleClass = env->FindClass("com/jsiudp/JsiUdpModule");
    if (!moduleClass) {
      __android_log_print(ANDROID_LOG_ERROR, "JsiUdp", "Failed to find JsiUdpModule class");
      if (didAttach) g_vm->DetachCurrentThread();
      return;
    }
  }

  // Get the emitDeviceEvent method if not already cached
  jmethodID emitMethod = g_emitDeviceEventMethod;
  if (!emitMethod) {
    emitMethod = env->GetStaticMethodID(moduleClass, "emitDeviceEvent", 
                                     "(Ljava/lang/String;Lcom/facebook/react/bridge/WritableMap;)V");
    if (!emitMethod) {
      __android_log_print(ANDROID_LOG_ERROR, "JsiUdp", "Failed to find emitDeviceEvent method");
      if (didAttach) g_vm->DetachCurrentThread();
      return;
    }
    g_emitDeviceEventMethod = emitMethod; // Cache for future use
  }
  
  // Try to get Arguments class from cache or load it using the proper classloader
  jclass argumentsClass = g_argumentsClass;
  if (!argumentsClass) {
    argumentsClass = FindClass(env, "com.facebook.react.bridge.Arguments");
    if (!argumentsClass) {
      __android_log_print(ANDROID_LOG_ERROR, "JsiUdp", "Failed to find Arguments class");
      if (didAttach) g_vm->DetachCurrentThread();
      return;
    }
    // Cache it for future use
    g_argumentsClass = (jclass)env->NewGlobalRef(argumentsClass);
  }
  
  // Get the createMap method
  jmethodID createMapMethod = g_createMapMethod;
  if (!createMapMethod) {
    createMapMethod = env->GetStaticMethodID(argumentsClass, "createMap", 
                                          "()Lcom/facebook/react/bridge/WritableMap;");
    if (!createMapMethod) {
      __android_log_print(ANDROID_LOG_ERROR, "JsiUdp", "Failed to find createMap method");
      if (didAttach) g_vm->DetachCurrentThread();
      return;
    }
    g_createMapMethod = createMapMethod; // Cache it
  }
  
  // Create the WritableMap
  jobject writableMap = env->CallStaticObjectMethod(argumentsClass, createMapMethod);
  if (!writableMap) {
    __android_log_print(ANDROID_LOG_ERROR, "JsiUdp", "Failed to create WritableMap");
    if (didAttach) g_vm->DetachCurrentThread();
    return;
  }
  
  // Get the WritableMap methods
  jclass writableMapClass = env->GetObjectClass(writableMap);
  if (!writableMapClass) {
    __android_log_print(ANDROID_LOG_ERROR, "JsiUdp", "Failed to get WritableMap class");
    env->DeleteLocalRef(writableMap);
    if (didAttach) g_vm->DetachCurrentThread();
    return;
  }
  
  jmethodID putStringMethod = env->GetMethodID(writableMapClass, "putString", "(Ljava/lang/String;Ljava/lang/String;)V");
  jmethodID putIntMethod = env->GetMethodID(writableMapClass, "putInt", "(Ljava/lang/String;I)V");
  jmethodID putNullMethod = env->GetMethodID(writableMapClass, "putNull", "(Ljava/lang/String;)V");
  
  if (!putStringMethod || !putIntMethod || !putNullMethod) {
    __android_log_print(ANDROID_LOG_ERROR, "JsiUdp", "Failed to find WritableMap methods");
    env->DeleteLocalRef(writableMapClass);
    env->DeleteLocalRef(writableMap);
    if (didAttach) g_vm->DetachCurrentThread();
    return;
  }
  
  // Fill the map with data
  jstring typeKey = env->NewStringUTF("type");
  jstring typeValue = env->NewStringUTF(eventType);
  env->CallVoidMethod(writableMap, putStringMethod, typeKey, typeValue);
  env->DeleteLocalRef(typeKey);
  env->DeleteLocalRef(typeValue);
  
  jstring socketIdKey = env->NewStringUTF("socketId");
  env->CallVoidMethod(writableMap, putIntMethod, socketIdKey, socketId);
  env->DeleteLocalRef(socketIdKey);
  
  jstring familyKey = env->NewStringUTF("family");
  jstring familyValue = env->NewStringUTF(family ? family : "");
  env->CallVoidMethod(writableMap, putStringMethod, familyKey, familyValue);
  env->DeleteLocalRef(familyKey);
  env->DeleteLocalRef(familyValue);
  
  jstring addressKey = env->NewStringUTF("address");
  jstring addressValue = env->NewStringUTF(address ? address : "");
  env->CallVoidMethod(writableMap, putStringMethod, addressKey, addressValue);
  env->DeleteLocalRef(addressKey);
  env->DeleteLocalRef(addressValue);
  
  jstring portKey = env->NewStringUTF("port");
  env->CallVoidMethod(writableMap, putIntMethod, portKey, port);
  env->DeleteLocalRef(portKey);
  
  jstring errorKey = env->NewStringUTF("error");
  if (errorMsg) {
    jstring errorValue = env->NewStringUTF(errorMsg);
    env->CallVoidMethod(writableMap, putStringMethod, errorKey, errorValue);
    env->DeleteLocalRef(errorValue);
  } else {
    env->CallVoidMethod(writableMap, putNullMethod, errorKey);
  }
  env->DeleteLocalRef(errorKey);
  
  // Handle data as Base64
  jstring dataKey = env->NewStringUTF("data");
  if (data && dataLength > 0) {
    // Directly create a byte array
    jbyteArray byteArray = env->NewByteArray(dataLength);
    if (byteArray) {
      env->SetByteArrayRegion(byteArray, 0, dataLength, (const jbyte*)data);
      
      // Find Base64 class
      jclass base64Class = env->FindClass("android/util/Base64");
      if (base64Class && !env->ExceptionCheck()) {
        jmethodID encodeMethod = env->GetStaticMethodID(base64Class, "encodeToString", "([BI)Ljava/lang/String;");
        if (encodeMethod) {
          jint flags = 2; // NO_WRAP
          jstring base64Str = (jstring)env->CallStaticObjectMethod(base64Class, encodeMethod, byteArray, flags);
          if (base64Str) {
            env->CallVoidMethod(writableMap, putStringMethod, dataKey, base64Str);
            env->DeleteLocalRef(base64Str);
          } else {
            env->CallVoidMethod(writableMap, putNullMethod, dataKey);
          }
        } else {
          env->CallVoidMethod(writableMap, putNullMethod, dataKey);
        }
        env->DeleteLocalRef(base64Class);
      } else {
        if (env->ExceptionCheck()) {
          env->ExceptionClear();
        }
        env->CallVoidMethod(writableMap, putNullMethod, dataKey);
      }
      env->DeleteLocalRef(byteArray);
    } else {
      env->CallVoidMethod(writableMap, putNullMethod, dataKey);
    }
  } else {
    env->CallVoidMethod(writableMap, putNullMethod, dataKey);
  }
  env->DeleteLocalRef(dataKey);
  
  // Emit the event
  jstring eventNameStr = env->NewStringUTF(eventName);
  env->CallStaticVoidMethod(moduleClass, emitMethod, eventNameStr, writableMap);
  
  // Check for exceptions
  if (env->ExceptionCheck()) {
    __android_log_print(ANDROID_LOG_ERROR, "JsiUdp", "Exception during event emission");
    env->ExceptionDescribe();
    env->ExceptionClear();
  } else {
    __android_log_print(ANDROID_LOG_DEBUG, "JsiUdp", "Successfully emitted event %s", eventName);
  }
  
  // Clean up
  env->DeleteLocalRef(eventNameStr);
  env->DeleteLocalRef(writableMapClass);
  env->DeleteLocalRef(writableMap);
  
  // Detach thread if we attached it
  if (didAttach) {
    g_vm->DetachCurrentThread();
  }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_jsiudp_JsiUdpModule_nativeInstall(JNIEnv *env, jclass clazz, jlong jsiPtr) {
  // Store a global reference to the class for future use
  if (!g_moduleClass) {
    g_moduleClass = (jclass)env->NewGlobalRef(clazz);
  }
  
  // Cache the emit method
  if (!g_emitDeviceEventMethod) {
    g_emitDeviceEventMethod = env->GetStaticMethodID(clazz, "emitDeviceEvent", 
                                                  "(Ljava/lang/String;Lcom/facebook/react/bridge/WritableMap;)V");
  }
  
  // Create the UdpManager
  auto runtime { reinterpret_cast<facebook::jsi::Runtime*>(jsiPtr) };
  manager = std::make_shared<jsiudp::UdpManager>(runtime);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_jsiudp_JsiUdpModule_nativeReset(JNIEnv *env, jclass _) {
  manager.reset();
}