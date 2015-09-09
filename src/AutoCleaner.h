//
//  AutoCleaner.h
//  console
//
//  Created by Jamol Bao on 9/9/15.
//  Copyright (c) 2015 Jamol Bao. All rights reserved.
//

#ifndef AutoCleaner_h
#define AutoCleaner_h

#include <functional>

namespace kuma {

class AutoCleaner
{
public:
    AutoCleaner(std::function<void (void)> f) : f_(f) {};
    ~AutoCleaner() { f_(); }
    
private:
    std::function<void (void)> f_;
};

} // namespace kuma

#define CONCAT_XY(x, y) x##y
#define MAKE_CLEANER(f, l) kuma::AutoCleaner CONCAT_XY(auto_cleaner_, l)(f)
#define AUTO_CLEAN(f) MAKE_CLEANER(f, __LINE__)

#endif
