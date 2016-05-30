#include<iostream>
using namespace std;
#include "zisync/kernel/notification/notification.h"

namespace zs 
{
  ZSNotificationCenter ZSNotificationCenter::default_center_;

  ZSNotificationCenter *DefaultNotificationCenter() {
    return &ZSNotificationCenter::default_center_;
  }

}
