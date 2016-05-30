#ifndef ZISYNC_KERNEL_NOTIFICATION_NOTIFICATION_H
#define ZISYNC_KERNEL_NOTIFICATION_NOTIFICATION_H

#include <stdint.h>
#include <map>
#include <set>
#include <memory>
#include <utility>
#include <string>
#include <functional>
#include <thread>
#include <chrono>
#include <tuple>

#include "zisync/kernel/platform/platform.h"
#include "zisync_kernel.h"
#include "zisync/kernel/libevent/libevent_base.h"
#ifdef __APPLE__
#import <Foundation/Foundation.h>
#define kZSNotificationUserInfoEventId @"kZSNotificationUserInfoEventId"
#define kZSNotificationUserInfoData @"kZSNotificationUserInfoData"
#endif

  //predefined notifications
#define ZSNotificationNameUpdateListSync "ZSNotificationNameUpdateListSync"
//#define ZSNotificationNameListSyncFinish "ZSNotificationNameSyncModify"
#define ZSNotificationNameUpdateSyncInfo "ZSNotificationNameUpdateSyncInfo"
//#define ZSNotificationNameSyncStart "ZSNotificationNameSyncStart"
//#define ZSNotificationNameSyncFinish "ZSNotificationNameSyncFinish"
#define ZSNotificationNameUpdateTreeStatus "ZSNotificationNameUpdateTreeStatus"
#define ZSNotificationNameUpdateTreePairStatus "ZSNotificationNameUpdateTreePairStatus"
#define ZSNotificationNameUpdateTransferListStatus "ZSNotificationNameUpdateTransferListStatus"
//#define ZSNotificationNameIndexFinish "ZSNotificationNameIndexFinish"
#define ZSNotificationNameUpdateBackupInfo "ZSNotificationNameUpdateBackupInfo"
#define ZSNotificationNameUpdateDiscoveredDevices "ZSNotificationNameUpdateDiscoveredDevices"

#define ZSNotificationNameUploadStatus "ZSNotificationNameUploadStatus"
#define ZSNotificationNameDownloadStatus "ZSNotificationNameDownloadStatus"

namespace zs {
  using std::string;
  using std::map;
  using std::set;
  using std::pair;
  using std::function;
  using std::unique_ptr;
  using std::shared_ptr;
  using std::placeholders::_1;
  using std::placeholders::_2;
  using std::placeholders::_3;

  //TODO: check at runtime whether user notifiation collide with predefined ones ?

  //@params
  //1. an string indicating event type
  //2. an int64_t indicating the specific instance of event; unique only for a specific event type
  //3. object pointer -- data associated with the occuring evnet, may be NULL if not needed
  typedef void ZSNotificationCallback(void*);

  typedef pair<intptr_t, intptr_t> ZSNotificationObserverKey;
  typedef function<ZSNotificationCallback> ZSNotificationObserver;
  typedef unique_ptr<ZSNotificationObserver> ZSNotificationObserverPtr;

  typedef pair<string, ZSNotificationObserverKey>
    ZSNotificationObserverItem;
  
  class ZSNotificationInfo {
  public:
    ZSNotificationInfo(std::string evt_name, int64_t evt_id, void *info_)
    : event_name(evt_name), event_id(evt_id), info(info_) {}
    
    std::string event_name;
    int64_t event_id;
    void *info;
  };

  class ZSNotificationCenter;
  ZSNotificationCenter *DefaultNotificationCenter();

  class ZSNotificationCenter {
    public:
      ZSNotificationCenter():mutex_(PTHREAD_MUTEX_ERRORCHECK){}
    
      friend ZSNotificationCenter *DefaultNotificationCenter();
    friend void LambdaPostNotification(void *ctx);
    
      template<typename ObjClass>
        void AddObserver(void(ObjClass::*method)(void*)
            , ObjClass *binded_obj
            , const string &event
            , void *sender = NULL) {
          MutexAuto mutex_auto(&mutex_);

          intptr_t sender_val = reinterpret_cast<intptr_t>(sender);
          map<ZSNotificationObserverKey, ZSNotificationObserverPtr> &event_observers
            = observers_[event][sender_val];

          ZSNotificationObserverKey observer_key = MakeObserverKey(method, binded_obj);
          if (event_observers.find(observer_key) != event_observers.end()) {
            return;
          }
          event_observers.emplace(make_pair(observer_key
                , ZSNotificationObserverPtr(CreateObserver(method, binded_obj))));
        }

      template<typename ObjClass>
        void RemoveObserver(void(ObjClass::*method)(void*)
            , ObjClass *binded_obj
            , const string &event
            , void *sender = NULL) {

          int try_lock_ret = mutex_.TryAquire();
          //1, mutex_ acquired by me, call RemoveObserverIntern directly
          //2, mutex_ acquired by this thread, should regiter this request
          int what_to_do;
#ifdef _WIN32
          switch (try_lock_ret) {
            case 0:
              if (mutex_missed_) {
                what_to_do = 2;
              }else {
                what_to_do = 1;
              }
              break;
            default:
              what_to_do = 1;
              mutex_.Aquire();
              break;
          }
#else
          int lock_ret;
          switch (try_lock_ret) {
            case 0:
              what_to_do = 1;
              break;
            case EBUSY:
              lock_ret = mutex_.Aquire();
              if (lock_ret == 0) {
                what_to_do = 1;
              }else if (lock_ret == EDEADLK) {
                what_to_do = 2;
              }else {
                assert(false);
              }
              break;
            default:
              assert(false);
              break;
          }
#endif
          int unlock_ret;
          switch (what_to_do) {
            case 1:
              MarkObserverDeleted(
                  event, method, binded_obj, sender);
              RemoveObserverIntern();
              unlock_ret = mutex_.Release();
              assert(unlock_ret == 0);
              break;
            case 2:
              MarkObserverDeleted(
                  event, method, binded_obj, sender);
              break;
            default:
              assert(false);
              break;
          }
        }

      //for event that has only single category, event_id is default value: 1
      void PostNotification(const string &event, void *info, void *sender) {
        PostNotification(event, 1, info, sender);
      }

      #ifdef __APPLE__
      void PostNotificationAtInterval(
          const string &event, void *data, int ms) {
          assert(data);
          MutexAuto mut(&mutex_event_id_);
          auto it = notify_data_.find(event);
          struct timeval delay = {0, 1000 * ms};
          if (it == notify_data_.end()) {
            notify_data_[event] = data;
            auto ctx = new std::tuple<string*, ZSNotificationCenter*>(new string(event), this);
            GetEventBase()->DispatchAsync(LambdaPostNotification, ctx, &delay);
          }else {
            CFBridgingRelease(it->second);
            notify_data_[event] = data;
          }

        }
        #endif

      void PostNotification(const string &event
          , int64_t event_id
          , void *info
          , void *sender) {
        assert(sender);
        MutexAuto mutex_auto(&mutex_);
#ifdef _WIN32
        mutex_missed_ = true;
#endif
        auto evet_observer_iter = observers_.find(event);
        if (evet_observer_iter == observers_.end()) {
          return;
        }
        intptr_t sender_val = reinterpret_cast<intptr_t>(sender);
        //        map<intptr_t, map<ZSNotificationObserverKey, ZSNotificationObserverPtr>>
        //          ::iterator sender_iter = evet_observer_iter->second.find(sender_val);

        auto sender_iter = evet_observer_iter->second.find(sender_val);
        if (sender_iter != evet_observer_iter->second.end()) {
          Notifiy(event, event_id, info
              , sender_iter->second.begin()
              , sender_iter->second.end());
        }

        sender_iter = evet_observer_iter->second.find(
            reinterpret_cast<intptr_t>((void*)0));
        if (sender_iter != evet_observer_iter->second.end()) {
          Notifiy(event, event_id, info
              , sender_iter->second.begin()
              , sender_iter->second.end());
        }

        RemoveObserverIntern();
#ifdef _WIN32
        mutex_missed_ = false;
#endif
      }

      int64_t GetEventId(const string &evnet) {
        MutexAuto mutex_auto(&mutex_event_id_);
        return ++event_ids_[evnet];
      }

    private:
      Mutex mutex_;
#ifdef _WIN32
      bool mutex_missed_;
#endif
      map<string,
        map<intptr_t,
        map<ZSNotificationObserverKey, ZSNotificationObserverPtr>>> observers_;

      map<ZSNotificationObserverItem, set<intptr_t>> observers_to_delete_;
      //this can be moved to other class
      Mutex mutex_event_id_;
      map<string, int64_t> event_ids_;

    map<string, void*> notify_data_;
    
    void HandleDelayedNotification(const string *notify_name) {
      MutexAuto mut(&mutex_event_id_);
      auto it = notify_data_.find(*notify_name);
      if (it == notify_data_.end()) {
        assert(false);
        return;
      }
      #ifdef __APPLE__
      @autoreleasepool {
        
        NSDictionary *userInfo = CFBridgingRelease(it->second);
        NSString *notifyName = [NSString stringWithUTF8String:notify_name->c_str()];
        [[NSNotificationCenter defaultCenter] postNotificationName:notifyName
                                                            object:nil
                                                          userInfo:userInfo];
      }
      #endif
      notify_data_.erase(it);
    }
    
    static void LambdaPostNotification(void *ctx) {
      string *notify_name;
      ZSNotificationCenter *notify_center;
      std::tie(notify_name, notify_center) = *(std::tuple<string*, ZSNotificationCenter*>*)ctx;
      notify_center->HandleDelayedNotification(notify_name);
      delete notify_name;
      delete (std::tuple<string*, ZSNotificationCenter*>*)ctx;
    }
    
    private:
      void Notifiy(
          const string& event, int64_t event_id, void *info 
          , map<ZSNotificationObserverKey, ZSNotificationObserverPtr>::const_iterator beg
          , map<ZSNotificationObserverKey, ZSNotificationObserverPtr>::const_iterator nd) {
        ZSNotificationInfo notification_data(event, event_id, info);
        for(;beg != nd; ++beg) {
          beg->second->operator()(&notification_data);
        }
      }

      template<typename ObjClass>
        ZSNotificationObserverKey MakeObserverKey(void(ObjClass::*method)(const string&, int64_t, void*)
            , ObjClass *obj) {
          intptr_t method_val = reinterpret_cast<intptr_t>(method);
          intptr_t obj_val = reinterpret_cast<intptr_t>(obj);
          return pair<intptr_t, intptr_t>(method_val, obj_val);
        }

      template<typename ObjClass>
        ZSNotificationObserver* CreateObserver(
            void(ObjClass::*pmethod)(const string&, int64_t, void*)
            , ObjClass *binded_obj) {
          return new ZSNotificationObserver(
              std::bind(pmethod, binded_obj, _1, _2, _3));
        }

      template<typename ObjClass>
        ZSNotificationObserverItem MakeObserverItem(
            const string &event
            , void(ObjClass::*method)(const string&, int64_t, void*)
            , ObjClass *obj) {
          ZSNotificationObserverKey observer_key = MakeObserverKey(
              method, obj
              );
          return ZSNotificationObserverItem(event, observer_key);
        }

      //RemoveObserver may be called inside notification callback,
      //in this case, lock will cause deadlock, just mark down
      template<typename ObjClass>
        void MarkObserverDeleted(
            const string &event
            , void(ObjClass::*method)(const string&, int64_t, void*)
            , ObjClass *obj 
            , void *sender) {
          ZSNotificationObserverItem item = MakeObserverItem(
              event, method, obj);
          intptr_t obj_val = reinterpret_cast<intptr_t>(sender);
          intptr_t n_val = reinterpret_cast<intptr_t>((void*)0);
          auto iter = observers_to_delete_.find(item);
          if (iter == observers_to_delete_.end()) {
            iter->second.insert(obj_val);
          }else {
            if (obj_val == n_val) {
              iter->second.clear();
            }
            iter->second.insert(obj_val);
          }
        }

        void RemoveObserverIntern() {
          for(auto observe_todel_iter = observers_to_delete_.begin()
              ; observe_todel_iter != observers_to_delete_.end()
              ; ++observe_todel_iter) {
            ZSNotificationObserverItem observer_item = observe_todel_iter->first;
            const string &event = observer_item.first;
            ZSNotificationObserverKey observer_key = observer_item.second;
            const set<intptr_t> &senders = observe_todel_iter->second;

            for(auto sender_val_iter = senders.begin()
                ; sender_val_iter != senders.end()
                ; ++sender_val_iter) {
              const intptr_t &sender_val = *sender_val_iter;

              auto evet_observer_iter = observers_.find(event);
              if (evet_observer_iter == observers_.end()){
                return;
              }

              //done care which sender, delete observing for them all
              if (!reinterpret_cast<void*>(sender_val)) {
                for(auto spec_observers_iter = evet_observer_iter->second.begin()
                    ; spec_observers_iter != evet_observer_iter->second.end()
                    ; ++spec_observers_iter) {

                  map<ZSNotificationObserverKey, ZSNotificationObserverPtr> &observers
                    = spec_observers_iter->second;

                  auto observer_iter = observers.find(observer_key);
                  if (observer_iter != observers.end()) {
                    observers.erase(observer_key);
                  }
                }
              }else {
//                auto blind_observers_iter = evet_observer_iter->second.find(reinterpret_cast<intptr_t>(NULL));
//                map<ZSNotificationObserverKey, ZSNotificationObserver> &observers 
//                  = blind_observers_iter->second;
//                if (observers.find(observer_key) != observers.end()) {
//                  for(auto sender_iter = evet_observer_iter->second.begin()
//                      ; sender_iter != evet_observer_iter->second.end()
//                      && sender_iter != blind_observers_iter
//                      ; ++sender_iter) {
//                    sender_iter->second[observer_key].reset(CreateObserver(method, binded_obj)); 
//                  }
//                  observers.erase(observer_key);
//                }

                auto spec_observers_iter = evet_observer_iter->second.find(sender_val);
                if (spec_observers_iter != evet_observer_iter->second.end()
                    && spec_observers_iter->second.find(observer_key) != spec_observers_iter->second.end()) {
                  spec_observers_iter->second.erase(observer_key);
                }
              }
            }
          }
          observers_to_delete_.clear();
        }

    private:
      static ZSNotificationCenter default_center_;

  };

}//namespace zs

#endif
