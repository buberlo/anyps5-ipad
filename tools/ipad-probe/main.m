/* SPDX-License-Identifier: MIT */
#import <UIKit/UIKit.h>
#import <sys/utsname.h>
#include "../gpu-probe/gpu_probe.h"

@interface ProbeDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@property(nonatomic, strong) UITextView *status;
@property(nonatomic) BOOL started;
@end
@implementation ProbeDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)app; (void)options;
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    UIViewController *controller = [UIViewController new];
    controller.view.backgroundColor = UIColor.systemBackgroundColor;
    self.status = [[UITextView alloc] initWithFrame:controller.view.bounds];
    self.status.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.status.editable = NO;
    self.status.font = [UIFont monospacedSystemFontOfSize:16 weight:UIFontWeightRegular];
    self.status.textContainerInset = UIEdgeInsetsMake(70, 24, 24, 24);
    self.status.text = @"AnyPS5 · GPU-Prüfung\n\nWarte auf aktiven Vordergrund …";
    [controller.view addSubview:self.status];
    self.window.rootViewController = controller;
    [self.window makeKeyAndVisible];
    return YES;
}
- (void)applicationDidBecomeActive:(UIApplication *)application {
    if (self.started) return;
    self.started = YES;
    application.idleTimerDisabled = YES;
    self.status.text = @"AnyPS5 · GPU-Prüfung\n\nGerät und Shader werden geprüft …\n\nDieser Test prüft MoltenVK direkt. Wine/FEX und ein Spiel werden separat getestet.";
    NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    NSString *reportPath = [documents stringByAppendingPathComponent:@"gpu-probe.jsonl"];
    NSString *shaderDirectory = NSBundle.mainBundle.resourcePath;
    struct utsname system; uname(&system);
    NSDictionary *metadata = @{@"schema":@1, @"stage":@"native_gpu", @"test":@"device_context",
        @"model":@(system.machine), @"os":NSProcessInfo.processInfo.operatingSystemVersionString,
        @"physical_memory_bytes":@(NSProcessInfo.processInfo.physicalMemory),
        @"application_state":@(application.applicationState),
        @"timestamp":@([[NSDate date] timeIntervalSince1970])};
    NSData *metadataJSON = [NSJSONSerialization dataWithJSONObject:metadata options:0 error:nil];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        FILE *report = fopen(reportPath.fileSystemRepresentation, "w");
        int code = 2;
        if (report) {
            fwrite(metadataJSON.bytes, 1, metadataJSON.length, report); fputc('\n', report); fflush(report);
            code = aps5_gpu_probe_run(shaderDirectory.fileSystemRepresentation, report);
            fclose(report);
        }
        NSString *log = [NSString stringWithContentsOfFile:reportPath encoding:NSUTF8StringEncoding error:nil] ?: @"Bericht konnte nicht geschrieben werden.";
        dispatch_async(dispatch_get_main_queue(), ^{
            application.idleTimerDisabled = NO;
            self.status.text = [NSString stringWithFormat:@"AnyPS5 · GPU-Prüfung\n\n%@\n\n%@\n\nBericht: Dateien → AnyPS5 GPU Probe → gpu-probe.jsonl",
                code == 0 ? @"GPU-Berechnung und Rücklesen bestanden." : @"Prüfung fehlgeschlagen. Details stehen im Bericht.", log];
        });
    });
}
@end

int main(int argc, char **argv) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(ProbeDelegate.class)); }
}
