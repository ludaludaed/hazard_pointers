#ifndef __INTRUSIVE_OPTIONS_H__
#define __INTRUSIVE_OPTIONS_H__


namespace lu {

template<class HookType>
struct base_hook {
  template<class Base>
  struct pack : public Base {
    using proto_value_traits = HookType;
  };
};

template<class ValueTraits>
struct value_traits {
  template<class Base>
  struct pack : public Base {
    using proto_value_traits = ValueTraits;
  };
};

template<class BucketTraits>
struct bucket_traits {
  template<class Base>
  struct pack : public Base {
    using proto_bucket_traits = BucketTraits;
  };
};

template<bool IsPower2Buckets>
struct is_power_2_buckets {
  template<class Base>
  struct pack : Base {
    static const bool is_power_2_buckets = IsPower2Buckets;
  };
};

template<class SizeType>
struct size_type {
  template<class Base>
  struct pack : public Base {
    using size_type = SizeType;
  };
};

template<class Hash>
struct hash {
  template<class Base>
  struct pack : public Base {
    using hash = Hash;
  };
};

template<class KeyOfValue>
struct key_of_value {
  template<class Base>
  struct pack : public Base {
    using key_of_value = KeyOfValue;
  };
};

template<class Equal>
struct equal {
  template<class Base>
  struct pack : public Base {
    using equal = Equal;
  };
};

template<class Compare>
struct compare {
  template<class Base>
  struct pack : public Base {
    using compare = Compare;
  };
};

template<bool IsAutoUnlink>
struct is_auto_unlink {
  template<class Base>
  struct pack : public Base {
    static const bool is_auto_unlink = IsAutoUnlink;
  };
};

template<bool StoreHash>
struct store_hash {
  template<class Base>
  struct pack : public Base {
    static const bool store_hash = StoreHash;
  };
};

template<class VoidPointer>
struct void_pointer {
  template<class Base>
  struct pack : public Base {
    using void_pointer = VoidPointer;
  };
};

template<class Tag>
struct tag {
  template<class Base>
  struct pack : public Base {
    using tag = Tag;
  };
};

}// namespace lu

#endif
