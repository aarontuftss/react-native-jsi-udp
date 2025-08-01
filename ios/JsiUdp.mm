#import "JsiUdp.h"

#import <React/RCTBridge+Private.h>
#import <React/RCTBridge.h>
#import <React/RCTUtils.h>
#import <ReactCommon/RCTTurboModule.h>
#import <React/RCTEventEmitter.h>
#import <React/RCTBridgeModule.h>
static __weak RCTBridge *g_udpBridge = nil;

@implementation JsiUdp

@synthesize bridge = _bridge;

RCT_EXPORT_MODULE()

std::shared_ptr<jsiudp::UdpManager> _manager;

- (void)invalidate {
  if (_manager) {
    _manager->closeAll();
  }
}

- (void)setBridge:(RCTBridge *)bridge {
  _bridge = bridge;
  g_udpBridge = bridge;
}

+ (BOOL)requiresMainQueueSetup {
  return YES;
}

void installApi(facebook::jsi::Runtime *runtime) {
  _manager = std::make_shared<jsiudp::UdpManager>(runtime);
}

RCT_EXPORT_BLOCKING_SYNCHRONOUS_METHOD(install) {
  RCTCxxBridge *cxxBridge = (RCTCxxBridge *)_bridge;
  if (cxxBridge.runtime != nullptr) {
    installApi((facebook::jsi::Runtime *)cxxBridge.runtime);
    return @(true);
  }
  return @(false);
}

// Don't compile this code when we build for the old architecture.
#ifdef RCT_NEW_ARCH_ENABLED
- (std::shared_ptr<facebook::react::TurboModule>)getTurboModule:
    (const facebook::react::ObjCTurboModule::InitParams &)params {
  RCTCxxBridge *cxxBridge = (RCTCxxBridge *)_bridge;
  installApi((facebook::jsi::Runtime *)cxxBridge.runtime);

  return std::make_shared<facebook::react::NativeJsiUdpSpecJSI>(params);
}
#endif

- (void)dealloc {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (instancetype)init {
  if (self = [super init]) {
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(handleAppStateChange:)
               name:UIApplicationWillResignActiveNotification
             object:nil];
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(handleAppStateChange:)
               name:UIApplicationDidBecomeActiveNotification
             object:nil];
  }
  return self;
}

- (void)handleAppStateChange:(NSNotification *)notification {
  return;
  if ([notification.name
          isEqualToString:UIApplicationWillResignActiveNotification]) {
    if (_manager) {
      _manager->suspendAll();
    }
  } else if ([notification.name
                 isEqualToString:UIApplicationDidBecomeActiveNotification]) {
    if (_manager) {
      _manager->resumeAll();
    }
  }
}

- (NSArray<NSString *> *)supportedEvents {
  return @[@"udp_message"];
}

+ (Class)moduleClass {
  return NSClassFromString(@"RCTEventEmitter");
}

- (void)startObserving {
  _hasListeners = YES;
  NSLog(@"JSI UDP Event listeners registered");
}

- (void)stopObserving {
  _hasListeners = NO;
  NSLog(@"JSI UDP Event listeners unregistered");
}

extern "C" void emit_udp_device_event(const char* eventName, const char* eventType,
                                     const void* data, size_t dataLength,
                                     const char* family, const char* address, int port,
                                     const char* errorMsg, int socketId) {
  NSString *eventNameStr = [NSString stringWithUTF8String:eventName];
  NSString *eventTypeStr = [NSString stringWithUTF8String:eventType];
  NSString *familyStr = [NSString stringWithUTF8String:family];
  NSString *addressStr = [NSString stringWithUTF8String:address];
  NSString *errorMsgStr = errorMsg ? [NSString stringWithUTF8String:errorMsg] : nil;

  // Convert binary data to base64 string for the bridge
  NSString *base64Data = nil;
  if (data && dataLength > 0) {
    NSData *nsData = [NSData dataWithBytes:data length:dataLength];
    base64Data = [nsData base64EncodedStringWithOptions:0];
  }


  NSDictionary *eventObject = @{
    @"type": eventTypeStr,
    @"socketId": @(socketId),
    @"data": base64Data ?: [NSNull null],
    @"family": familyStr,
    @"address": addressStr,
    @"port": @(port),
    @"error": errorMsgStr ? errorMsgStr : [NSNull null]
  };

  NSLog(@"Emitting event: %@ with data: %@", eventNameStr, eventObject);

  dispatch_async(dispatch_get_main_queue(), ^{
    if (!g_udpBridge) {
      NSLog(@"Bridge is null, can't emit event");
      return;
    }
    
    // Try to get the module directly
    JsiUdp *module = (JsiUdp *)[g_udpBridge moduleForClass:[JsiUdp class]];
    if (module && module->_hasListeners) {
      // Send using our module (which subclasses RCTEventEmitter)
      [module sendEventWithName:eventNameStr body:eventObject];
      NSLog(@"Sent event via module: %@", eventNameStr);
    } else {
      // Fallback to using RCTDeviceEventEmitter directly
      NSLog(@"Module not found or no listeners, trying RCTDeviceEventEmitter");
      id<RCTBridgeModule> emitter = [g_udpBridge moduleForName:@"RCTDeviceEventEmitter"];
      if ([emitter respondsToSelector:@selector(sendEventWithName:body:)]) {
        [(id)emitter sendEventWithName:eventNameStr body:eventObject];
        NSLog(@"Sent event via RCTDeviceEventEmitter: %@", eventNameStr);
      } else {
        NSLog(@"Failed to find a valid event emitter");
      }
    }
  });
}

@end
