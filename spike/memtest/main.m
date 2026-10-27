// shows the spike's report on screen and saves it to Documents/memtest-<variant>.txt
#import <UIKit/UIKit.h>
#include <os/log.h>
#include "spike.h"

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property(strong) UIWindow* window;
@end

@implementation AppDelegate
- (BOOL)application:(UIApplication*)app didFinishLaunchingWithOptions:(NSDictionary*)opts {
    static char buf[16384];
    spike_run(buf, sizeof buf, 0);
    NSString* text = [NSString stringWithUTF8String:buf];
    NSString* variant = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"MemtestVariant"] ?: @"unknown";
    NSString* docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    [text writeToFile:[docs stringByAppendingPathComponent:[NSString stringWithFormat:@"memtest-%@.txt", variant]]
           atomically:YES encoding:NSUTF8StringEncoding error:nil];
    os_log_t log = os_log_create("com.muhammad.memtest", "spike");
    // %{public}s: the report would be <private> in the log otherwise
    os_log(log, "variant %{public}s\n%{public}s", variant.UTF8String, text.UTF8String);
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    UIViewController* vc = [UIViewController new];
    UITextView* tv = [[UITextView alloc] initWithFrame:vc.view.bounds];
    tv.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    tv.font = [UIFont monospacedSystemFontOfSize:9 weight:UIFontWeightRegular];
    tv.editable = NO;
    tv.text = [NSString stringWithFormat:@"variant: %@\n%@", variant, text];
    [vc.view addSubview:tv];
    self.window.rootViewController = vc;
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char* argv[]) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class])); }
}
