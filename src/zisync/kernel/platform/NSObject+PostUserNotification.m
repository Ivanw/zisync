//
//  NSObject+PostUserNotification.m
//  PlusyncMac
//
//  Created by Zi on 15/8/11.
//  Copyright (c) 2015年 plusync. All rights reserved.
//

#import "NSObject+PostUserNotification.h"
#import "PLUserDefaultsKeys.h"

@implementation NSObject (PostUserNotification)

+ (void)postUserNotificationTitle:(NSString*)title
                  informativeText:(NSString*)message {
  
  NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
  BOOL showUserNotification = [defaults boolForKey:kPLShowNotification];
  
  if (showUserNotification) {
    NSUserNotification *notification = [[NSUserNotification alloc] init];
    notification.title = title;
    notification.informativeText = message;
    
    NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
    [center deliverNotification:notification];
  }
}
@end
