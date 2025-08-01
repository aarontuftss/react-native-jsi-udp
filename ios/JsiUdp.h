#ifdef __cplusplus
#import "react-native-jsi-udp.h"
#endif

#ifdef RCT_NEW_ARCH_ENABLED
#import "RNJsiUdpSpec.h"

@interface JsiUdp : NSObject <NativeJsiUdpSpec>
#else
#import <React/RCTBridgeModule.h>
#import <React/RCTEventEmitter.h>

@interface JsiUdp : RCTEventEmitter <RCTBridgeModule>
{
  BOOL _hasListeners;
}
#endif

@end
