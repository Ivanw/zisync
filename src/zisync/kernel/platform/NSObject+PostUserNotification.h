//
//  NSObject+PostUserNotification.h
//  PlusyncMac
//
//  Created by Zi on 15/8/11.
//  Copyright (c) 2015年 plusync. All rights reserved.
//

#import <Foundation/Foundation.h>

@interface NSObject (PostUserNotification)

+ (void)postUserNotificationTitle:(NSString*)title
                  informativeText:(NSString*)message;

@end
