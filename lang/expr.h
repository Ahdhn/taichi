#pragma once

#include <taichi/common/util.h>
#include "../headers/common.h"
#include "address.h"
#include "visitor.h"

TC_NAMESPACE_BEGIN

namespace Tlang {

enum class DataType : int {
  f16,
  f32,
  f64,
  i8,
  i16,
  i32,
  i64,
  u8,
  u16,
  u32,
  u64,
  unknown
};

template <typename T>
DataType get_data_type() {
  if (std::is_same<T, float32>()) {
    return DataType::f32;
  } else if (std::is_same<T, int32>()) {
    return DataType::i32;
  } else {
    TC_NOT_IMPLEMENTED;
  }
  return DataType::unknown;
}

inline std::string data_type_name(DataType t) {
  static std::map<DataType, std::string> data_type_names;
  if (data_type_names.empty()) {
#define REGISTER_DATA_TYPE(i, j) data_type_names[DataType::i] = #j;
    REGISTER_DATA_TYPE(f16, float16);
    REGISTER_DATA_TYPE(f32, float32);
    REGISTER_DATA_TYPE(f64, float64);
    REGISTER_DATA_TYPE(i8, int8);
    REGISTER_DATA_TYPE(i16, int16);
    REGISTER_DATA_TYPE(i32, int32);
    REGISTER_DATA_TYPE(i64, int64);
    REGISTER_DATA_TYPE(u8, uint8);
    REGISTER_DATA_TYPE(u16, uint16);
    REGISTER_DATA_TYPE(u32, uint32);
    REGISTER_DATA_TYPE(u64, uint64);
#undef REGISTER_DATA_TYPE
  }
  return data_type_names[t];
}

// TODO: do we need polymorphism here?
class Node {
 private:
  Address _addr;
  static int counter;

 public:
  static void reset_counter() {
    counter = 0;
  }

  // TODO: rename
  enum class Type : int {
    mul,
    add,
    sub,
    div,
    mod,
    load,
    store,
    pointer,
    combine,
    index,
    addr,
    adapter_store,
    adapter_load,
    imm,
    floor,
    max,
    min,
    cast,
    land,
    shr,
    shl,
    cmp,
    select,
  };

  enum class CmpType { eq, ne, le, lt };

  using NodeType = Type;

  std::vector<Expr> ch;       // Four child max
  std::vector<Expr> members;  // for vectorized instructions
  Type type;
  DataType data_type;
  std::string var_name;
  float64 _value;
  int id;
  int num_groups_;
  bool is_vectorized;
  static std::map<Type, std::string> node_type_names;
  std::string name_;

  std::string name() {
    return name_;
  }

  void name(std::string s) {
    name_ = s;
  }

  int group_size() {
    return (int)members.size();
  }

  int &num_groups() {
    return num_groups_;
  }

  int vv_width() {
    return group_size() * num_groups();
  }

  Node(const Node &) = delete;

  Node(Type type) : type(type) {
    is_vectorized = false;
    data_type = DataType::f32;
    id = counter++;
    _value = 0;
  }

  std::string data_type_name() {
    return taichi::Tlang::data_type_name(data_type);
  }

  std::string node_type_name() {
    if (node_type_names.empty()) {
#define REGISTER_NODE_TYPE(i) node_type_names[NodeType::i] = #i;
      REGISTER_NODE_TYPE(mul);
      REGISTER_NODE_TYPE(add);
      REGISTER_NODE_TYPE(sub);
      REGISTER_NODE_TYPE(div);
      REGISTER_NODE_TYPE(mod);
      REGISTER_NODE_TYPE(load);
      REGISTER_NODE_TYPE(store);
      REGISTER_NODE_TYPE(combine);
      REGISTER_NODE_TYPE(addr);
      REGISTER_NODE_TYPE(pointer);
      REGISTER_NODE_TYPE(adapter_store);
      REGISTER_NODE_TYPE(adapter_load);
      REGISTER_NODE_TYPE(imm);
      REGISTER_NODE_TYPE(index);
      REGISTER_NODE_TYPE(floor);
      REGISTER_NODE_TYPE(max);
      REGISTER_NODE_TYPE(min);
      REGISTER_NODE_TYPE(cast);
      REGISTER_NODE_TYPE(land);
      REGISTER_NODE_TYPE(shr);
      REGISTER_NODE_TYPE(shl);
      REGISTER_NODE_TYPE(cmp);
      REGISTER_NODE_TYPE(select);
    }
    return node_type_names[type];
  }

  Address &get_address_() {  // TODO: remove this hack
    return _addr;
  }

  Address &get_address() {
    TC_ASSERT(type == Type::addr);
    return _addr;
  }

  Address &addr();

  Node(Type type, Expr ch0);

  Node(Type type, Expr ch0, Expr ch1);

  Node(Type type, Expr ch0, Expr ch1, Expr ch2);

  int member_id(const Expr &expr) const;

  template <typename T>
  T &value() {
    return *reinterpret_cast<T *>(&_value);
  }
};

using NodeType = Node::Type;
using CmpType = Node::CmpType;

class Visitor;

// Reference counted...
class Expr {
 private:
  Handle<Node> node;
  static bool allow_store;

 public:
  using Type = Node::Type;

  static void set_allow_store(bool val) {
    allow_store = val;
  }

  auto &get_node() {
    return node;
  }

  Expr() {
  }

  /*
  Expr(float64 val) {
    // create a constant node
    node = std::make_shared<Node>(NodeType::imm);
    node->value<float64>() = val;
  }
  */

  Expr(Handle<Node> node) : node(node) {
  }

  template <typename... Args>
  static Expr create(Args &&... args) {
    return Expr(std::make_shared<Node>(std::forward<Args>(args)...));
  }

  template <typename T>
  static Expr create_imm(T t) {
    auto e = create(Type::imm);
    e->value<T>() = t;
    return e;
  }

  static Expr index(int i) {
    auto e = create(Type::index);
    e->value<int>() = i;
    e->data_type = DataType::i32;
    return e;
  }

  static Expr load_if_pointer(const Expr &in) {
    if (in->type == NodeType::pointer) {
      return create(NodeType::load, in);
    } else {
      return in;
    }
  }

#define BINARY_OP(op, name)                                      \
  Expr operator op(const Expr &o) const {                        \
    TC_ASSERT(node->data_type == o->data_type)                   \
    auto t = Expr::create(NodeType::name, load_if_pointer(node), \
                          load_if_pointer(o.node));              \
    t->data_type = o->data_type;                                 \
    return t;                                                    \
  }

  BINARY_OP(*, mul);
  BINARY_OP(+, add);
  BINARY_OP(-, sub);
  BINARY_OP(/, div);
  BINARY_OP(%, mod);
  BINARY_OP(&, land);
  BINARY_OP(>>, shr);
  BINARY_OP(<<, shl);
#undef BINARY_OP

  // ch[0] = address
  // ch[1] = data
  Expr store(const Expr &pointer, const Expr &e) {
    if (!node) {
      node = std::make_shared<Node>(NodeType::combine);
    }
    auto n = std::make_shared<Node>(NodeType::store);
    TC_ASSERT(pointer->type == NodeType::pointer);
    n->ch.push_back(pointer);
    n->ch.push_back(e);
    Expr store_e(n);
    node->ch.push_back(n);
    return store_e;
  }

  Node *operator->() {
    return node.get();
  }

  const Node *operator->() const {
    return node.get();
  }

  bool operator<(const Expr &o) const {
    return node.get() < o.node.get();
  }

  operator bool() const {
    return node.get() != nullptr;
  }

  operator void *() const {
    return (void *)node.get();
  }

  bool operator==(const Expr &o) const {
    return (void *)(*this) == (void *)o;
  }

  bool operator!=(const Expr &o) const {
    return (void *)(*this) != (void *)o;
  }

  void accept(Visitor &visitor) {
    if (visitor.order == Visitor::Order::parent_first) {
      visitor.visit(*this);
    }
    for (auto &c : this->node->ch) {
      c.accept(visitor);
    }
    if (visitor.order == Visitor::Order::child_first) {
      visitor.visit(*this);
    }
  }

  Expr &operator=(const Expr &o);

  Expr operator[](const Expr &i);

  Expr &operator[](int i) {
    TC_ASSERT(0 <= i && i < (int)node->ch.size());
    return node->ch[i];
  }

  void set(const Expr &o) {
    node = o.node;
  }

  template <typename T>
  void set(int i, const T &t) {
  }

  Expr &name(std::string s) {
    node->name(s);
    return *this;
  }

  /*
  Expr operator!=(const Expr &o) {
    auto n = create(NodeType::cmp, *this, o);
    n->value<CmpType>() = CmpType::ne;
    return n;
  }

  Expr operator<(const Expr &o) {
    auto n = create(NodeType::cmp, *this, o);
    n->value<CmpType>() = CmpType::lt;
    return n;
  }
  */

  Node *ptr() {
    return node.get();
  }
};

using Index = Expr;

inline Expr cmp_ne(const Expr &a, const Expr &b) {
  auto n = Expr::create(NodeType::cmp, a, b);
  n->value<CmpType>() = CmpType::ne;
  return n;
}

inline Expr cmp_lt(const Expr &a, const Expr &b) {
  auto n = Expr::create(NodeType::cmp, a, b);
  n->value<CmpType>() = CmpType::lt;
  return n;
}

inline bool prior_to(Address address1, Address address2) {
  return address1.same_type(address2) &&
         address1.offset() + 1 == address2.offset();
}

inline bool prior_to(Expr &a, Expr &b) {
  TC_ASSERT(a->type == NodeType::pointer && b->type == NodeType::pointer);
  return prior_to(a->ch[0]->get_address(), b->ch[0]->get_address());
}

inline Node::Node(Type type, Expr ch0) : Node(type) {
  ch.resize(1);
  ch[0] = ch0;
}

inline Node::Node(Type type, Expr ch0, Expr ch1) : Node(type) {
  ch.resize(2);
  ch[0] = ch0;
  ch[1] = ch1;
}

inline Node::Node(Type type, Expr ch0, Expr ch1, Expr ch2) : Node(type) {
  ch.resize(3);
  ch[0] = ch0;
  ch[1] = ch1;
  ch[2] = ch2;
}

inline Expr placeholder() {
  auto n = std::make_shared<Node>(NodeType::addr);
  return Expr(n);
}

inline int Node::member_id(const Expr &expr) const {
  for (int i = 0; i < (int)members.size(); i++) {
    if (members[i] == expr) {
      return i;
    }
  }
  return -1;
}

inline Address &Node::addr() {
  TC_ASSERT(type == Type::load || type == Type::store);
  TC_ASSERT(ch.size());
  TC_ASSERT(ch[0]->type == Type::pointer);
  return ch[0]->ch[0]->get_address();
}

inline Expr load(const Expr &addr) {
  auto expr = Expr::create(NodeType::load);
  expr->ch.push_back(addr);
  return expr;
}

inline Expr select(Expr mask, Expr true_val, Expr false_val) {
  return Expr::create(NodeType::select, mask, true_val, false_val);
}

}  // namespace Tlang

TC_NAMESPACE_END
