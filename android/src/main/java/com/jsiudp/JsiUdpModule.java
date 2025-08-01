package com.jsiudp;

import androidx.annotation.NonNull;

import com.facebook.react.bridge.Promise;
import com.facebook.react.bridge.ReactApplicationContext;
import com.facebook.react.bridge.ReactContextBaseJavaModule;
import com.facebook.react.bridge.ReactMethod;
import com.facebook.react.module.annotations.ReactModule;
import com.facebook.react.turbomodule.core.CallInvokerHolderImpl;
import androidx.annotation.Nullable;
import com.facebook.react.modules.core.DeviceEventManagerModule;
import com.facebook.react.bridge.WritableMap;
import com.facebook.react.bridge.Arguments;
import java.lang.ref.WeakReference;

@ReactModule(name = JsiUdpModule.NAME)
public class JsiUdpModule extends ReactContextBaseJavaModule {
  public static final String NAME = "JsiUdp";
  private boolean mInstalled = false;
  private static WeakReference<ReactApplicationContext> sReactContext;

  public JsiUdpModule(ReactApplicationContext reactContext) {
    super(reactContext);
    sReactContext = new WeakReference<>(reactContext);
  }

  @Override
  @NonNull
  public String getName() {
    return NAME;
  }

  private static native void nativeInstall(long jsiPtr, CallInvokerHolderImpl jsCallInvokerHolder);

  private static native void nativeReset();

  @Override
  public void invalidate() {
    super.invalidate();
    if (mInstalled) {
      nativeReset();
    }
  }

  @ReactMethod(isBlockingSynchronousMethod = true)
  public boolean install() {
    try {
      System.loadLibrary("jsiudp");

      ReactApplicationContext context = getReactApplicationContext();
      CallInvokerHolderImpl holder = (CallInvokerHolderImpl) context.getCatalystInstance().getJSCallInvokerHolder();
      nativeInstall(context.getJavaScriptContextHolder().get(), holder);
      mInstalled = true;
      return true;
    } catch (Exception exception) {
      return false;
    }
  }

  public static void emitDeviceEvent(String eventName, @Nullable WritableMap params) {
    ReactApplicationContext context = sReactContext.get();
    if (context != null) {
      try {
        // Send on main thread to avoid React Native threading issues
        context.runOnUiQueueThread(() -> {
          try {
            if (!context.hasActiveReactInstance()) {
              android.util.Log.w("JsiUdp", "No active React instance, can't emit events");
              return;
            }
            context.getJSModule(DeviceEventManagerModule.RCTDeviceEventEmitter.class)
                .emit(eventName, params);
            android.util.Log.d("JsiUdp", "Emitted event: " + eventName);
          } catch (Exception e) {
            android.util.Log.e("JsiUdp", "Error emitting event on UI thread: " + e.getMessage());
          }
        });
      } catch (Exception e) {
        android.util.Log.e("JsiUdp", "Error scheduling event emission: " + e.getMessage());
      }
    } else {
      android.util.Log.e("JsiUdp", "React context is null, can't emit events");
    }
  }
  
  public static ReactApplicationContext getReactContext() {
    return sReactContext != null ? sReactContext.get() : null;
  }
}
