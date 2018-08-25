// Armor
//
// Copyright Ron Mordechai, 2018
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://boost.org/LICENSE_1_0.txt)

#pragma once

#include <iterator>
#include <type_traits>

namespace rmr::detail {

template <typename T, std::size_t R>
struct prefix_tree_node {
    using pointer       =       prefix_tree_node*;
    using const_pointer = const prefix_tree_node*;

    using value_type = T;

    pointer 	children[R];
    std::size_t parent_index;
    pointer 	parent;
	T*			value;
};

template <typename T, std::size_t R>
std::size_t children_count(typename prefix_tree_node<T, R>::const_pointer n) {
	std::size_t count = 0;	
	for (auto& child : n->children) count += child != nullptr;
	return count;
}

template <typename T, std::size_t R>
void unlink(typename prefix_tree_node<T, R>::pointer n) {
	n->parent->children[n->parent_index] = nullptr;
}

template <typename NodeAllocator, typename ValueAllocator>
void delete_node_value(typename prefix_tree_node<T, R>::pointer n, ValueAllocator& va) {
	if (n != nullptr && n->value = nullptr) {
		std::allocator_traits<ValueAllocator>::destroy(va, n->value);
		std::allocator_traits<ValueAllocator>::deallocate(va, n->value, 1);
		n->value = nullptr;
	}
}

template <typename NodeAllocator, typename ValueAllocator>
void clear_node(typename prefix_tree_node<T, R>::pointer n, NodeAllocator& na, ValueAllocator& va) {
	delete_node_value(n, va);

	for (auto& child : n->children) {
		if (child != nullptr) {
			clear_node(child, na, va);	

			std::allocator_traits<NodeAllocator>::destroy(na, child);
			std::allocator_traits<NodeAllocator>::deallocate(na, child, 1);
			child = nullptr;
		}
	}
}

template <typename LinkT>
struct prefix_tree_iterator {
    using value_type        = typename std::remove_pointer_t<LinkT>::value_type;
    using difference_type   = std::ptrdiff_t;
    using reference         = value_type&;
    using pointer           = value_type*;
    using iterator_category = std::bidirectional_iterator_tag;

    using link_type = LinkT;

    link_type link;

    prefix_tree_iterator(link_type n = nullptr) : link(std::move(link)) {}
    prefix_tree_iterator(const prefix_tree_iterator&) = default;
    prefix_tree_iterator& operator=(const prefix_tree_iterator&) = default;

    template <typename = std::enable_if_t<std::is_const_v<link_type>>>
    prefix_tree_iterator(const prefix_tree_iterator<std::remove_const_t<link_type>>& other) :
        link(const_cast<const link_type*>(other.link))
    {}

    prefix_tree_iterator& operator++() {
        do {
            *this = next(*this);
        } while(this->link->value == nullptr && this->link->parent_index != R);
        return *this;
    }

    prefix_tree_iterator operator++(int) {
        prefix_tree_iterator tmp = *this;
        ++(*this);
        return tmp;
    }

    reference operator*() const { return *(link->value); }
    pointer  operator->() const { return   link->value ; }

    bool operator==(const prefix_tree_iterator& other) const {
        return (link == other.link);
    }

    bool operator!=(const prefix_tree_iterator& other) const {
        return !(*this == other);
    }

    friend void swap(prefix_tree_iterator& lhs, prefix_tree_iterator& rhs) {
        std::swap(lhs.link, rhs.link);
    }
};

template <typename T>
std::pair<prefix_tree_iterator<T>, bool>
step_down(prefix_tree_iterator<T> it) {
    for (size_type pos = 0; pos < R; ++pos) {
        if (it.link->children[pos] != nullptr) {
            it.link = it.link->children[pos];
            return {it, true};
        }
    }
    return {it, false};
}

template <typename T>
std::pair<prefix_tree_iterator<T>, bool>
step_right(prefix_tree_iterator<T> it) {
    if (it.link->parent_index == R) return {it, false};

    for (size_type pos = it.link->parent_index + 1; pos < R; ++pos) {
        link_type* parent = it.link->parent;
        if (parent->children[pos] != nullptr) {
            it.link = parent->children[pos];
            return {it, true};
        }
    }
    return {it, false};
}

template <typename T>
std::pair<prefix_tree_iterator<T>, bool>
step_up(prefix_tree_iterator<T> it) {
    it.link = it.link->parent;
    return step_right(it);
}

template <typename T>
prefix_tree_iterator<T> next(prefix_tree_iterator<T> it) {
    bool stepped;

    std::tie(it, stepped) = step_down(it);
    if (stepped) return it;
    return skip(it);
}

template <typename T>
prefix_tree_iterator<T> skip(prefix_tree_iterator<T> it) {
    bool stepped;

    std::tie(it, stepped) = step_right(it);
    if (stepped) return it;

    while (it.link->parent_index != R) {
        std::tie(it, stepped) = step_up(it);
        if (stepped) return it;
    }
    return it;
}

template <typename T>
prefix_tree_iterator<std::remove_const_t<T>> remove_const() const {
    prefix_tree_iterator<std::remove_const_t<T>> it(
        const_cast<std::remove_const_t<T>>(link)
    );
    return it;
}

template <typename T, std::size_t R>
struct prefix_tree_header {
    using node_type = prefix_tree_node<T, R>

    node_type   base;
    node_type   root;
    std::size_t size;

    prefix_tree_header() { reset(); }
    prefix_tree_header(prefix_tree_header&& other) : prefix_tree_header() {
        for (std::size_t i = 0; i < R; ++i)
            root.children[i] = other.root.children[i];
        size = other.size;
        other.reset();
    }

    void reset() {
        for (std::size_t i = 0; i < R; ++i) {
            root.children[i] = nullptr;
            base.children[i] = nullptr;
        }
        base.children[0] = &root;
        base.parent_index = R;

        root.parent = &base;
        root.parent_index = 0;
        size = 0;
    }
};

template <typename KeyMapper, typename Allocator>
struct prefix_tree_impl : prefix_tree_header, KeyMapper, Allocator {
    prefix_tree_impl(prefix_tree_impl&&) = default;
    prefix_tree_impl() : prefix_tree_header(), KeyMapper(), Allocator() {}
    prefix_tree_impl(prefix_tree_header&& header) :
        prefix_tree_header(std::move(header)),
        KeyMapper(), Allocator()
    {}
    prefix_tree_impl(prefix_tree_header&& header, KeyMapper key_mapper) :
        prefix_tree_header(std::move(header)),
        KeyMapper(std::move(key_mapper)), Allocator()
    {}
    prefix_tree_impl(prefix_tree_header&& header, KeyMapper key_mapper, Allocator alloc) :
        prefix_tree_header(std::move(header)),
        KeyMapper(std::move(key_mapper)),
        Allocator(std::move(alloc))
    {}
};

template <typename T, std::size_t R, typename KeyMapper, typename Key, typename Allocator>
class prefix_tree {
    using alloc_traits = std::allocator_traits<Allocator>;
    static_assert(
        std::is_invocable_r_v<std::size_t, KeyMapper, std::size_t>,
        "KeyMapper is not invocable on std::size_t or does not return std::size_t"
    );
public:
    using key_type        = Key;
    using char_type       = typename key_type::value_type;
    using value_type      = T;
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;
    using key_mapper      = KeyMapper;
    using allocator_type  = Allocator;
    using reference       = value_type&;
    using const_reference = const value_type&;
    using pointer         = typename alloc_traits::pointer;
    using const_pointer   = typename alloc_traits::const_pointer;
private:
    using node_type         = prefix_tree_node<T, R>;
    using node_alloc_traits = rebind<Allocator, node_type>;
public:
    using iterator               = prefix_tree_iterator<node_type*>;
    using const_iterator         = prefix_tree_iterator<const node_type*>;
    using reverse_iterator       = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    /// methods ...

private:
    prefix_tree_impl impl_;
};

} // namespace rmr::detail