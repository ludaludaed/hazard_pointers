#ifndef __INTRUSIVE_NODE_HOLDER_H__
#define __INTRUSIVE_NODE_HOLDER_H__

namespace lu {
namespace detail {

template <class NodeType, class Tag>
class NodeHolder : public NodeType {};

}// namespace detail
}// namespace lu

#endif
