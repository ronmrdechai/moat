// Armor
//
// Copyright Ron Mordechai, 2018
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://boost.org/LICENSE_1_0.txt)

#pragma once

#include <algorithm>
#include <cassert>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

#include <rmr/functors.h>

namespace rmr {

namespace detail {

template <typename...> struct rebind;
template <template <typename> typename Type, typename... Inner, typename... New>
struct rebind<Type<Inner...>, New...> {
    using type = Type<New...>;
};

} // namespace detail

template <
    typename T,
    std::size_t R,
    typename KeyMapper = identity<std::size_t>,
    typename Key = std::string,
    typename Allocator = std::allocator<std::pair<const Key, T>>
>
class trie_map {
    using alloc_traits = std::allocator_traits<Allocator>;
    static_assert(
        std::is_invocable_r_v<std::size_t, KeyMapper, std::size_t>, 
        "KeyMapper is not invocable on std::size_t or does not return std::size_t"
    );

    static constexpr std::size_t R_END = std::numeric_limits<std::size_t>::max();
    static_assert(R < R_END, "R must be less than UINT_MAX");
public:
    using key_type        = Key;
    using char_type       = typename key_type::value_type;
    using mapped_type     = T;
    using value_type      = std::pair<const key_type, mapped_type>;
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;
    using key_mapper      = KeyMapper;
    using allocator_type  = Allocator;
    using reference       = value_type&;
    using const_reference = const value_type&;
    using pointer         = typename alloc_traits::pointer;
    using const_pointer   = typename alloc_traits::const_pointer;

public:
    class node_type;
private:
    using node_allocator_type = typename detail::rebind<allocator_type, node_type>::type;
    using node_alloc_traits = std::allocator_traits<node_allocator_type>;
public:
    class node_type {
    public:
        using key_type       = trie_map::key_type;
        using mapped_type    = trie_map::mapped_type;
        using value_type     = trie_map::value_type;
        using allocator_type = trie_map::allocator_type;

        constexpr node_type() : value_(nullptr) {}
        constexpr node_type(allocator_type allocator) :
            value_(nullptr), alloc_(std::move(allocator))
        {} 
        node_type(node_type&& other) :
            value_(std::move(other.value_)), alloc_(std::move(other.alloc_))
        { other.value_ = nullptr; }

        node_type& operator=(node_type&& other) {
            value_ = std::move(other.value_);
            other.value_ = nullptr;
            if (node_alloc_traits::propagate_on_container_move_assignment::value) {
                alloc_ = std::move(other.alloc_);
            }
            return *this;
        }

        bool             empty() const { return value_ == nullptr; }
        explicit operator bool() const { return !empty(); }

        key_type&       key()    const { return const_cast<key_type&>(value_->first); }
        mapped_type&    mapped() const { return value_->second; }
        value_type&     value()  const { return *value_; }

        node_allocator_type get_allocator() const { return *alloc_; }

        void swap(node_type& nh) {
            std::swap(value_,     nh.value_);
            if (node_alloc_traits::propagate_on_container_swap::value) {
                std::swap(alloc_, nh.alloc_);
            }
        }
        friend void swap(node_type& x, node_type& y) {
            x.swap(y);
        }

    private:
        pointer                            value_;
        std::optional<node_allocator_type> alloc_;

        friend class trie_map;
    };

    struct link_type;
    using link_allocator_type = typename detail::rebind<allocator_type, link_type>::type;
    using link_alloc_traits = std::allocator_traits<link_allocator_type>;
    struct link_type {
        std::array<link_type*, R> children = { nullptr };
        std::size_t parent_index = 0;
        link_type* parent = nullptr;
        node_type* handle = nullptr;

        void destroy(
            node_allocator_type& node_allocator, link_allocator_type& link_allocator
        ) {
            if (handle != nullptr) {
                node_alloc_traits::deallocate(node_allocator, handle, 1);
                handle = nullptr;
            }

            for (auto& child : children) {
                if (child != nullptr) {
                    child->destroy(node_allocator, link_allocator);
                    link_alloc_traits::deallocate(link_allocator, child, 1);
                    child = nullptr;
                }
            }
        }
    };

public:
    trie_map() : size_(0) { init(); }
    explicit trie_map(key_mapper f, allocator_type allocator = allocator_type()) :
        size_(0), alloc_(std::move(allocator)), key_map_(std::move(f))
    { init(); }
    explicit trie_map(allocator_type allocator) :
        size_(0), alloc_(std::move(allocator)) 
    { init(); }

    trie_map(trie_map&&) = default;
    trie_map(trie_map&& other, allocator_type allocator) :
        base_(std::move(other.base_)),
        root_(std::move(other.root_)),
        size_(0),
        alloc_(std::move(allocator)),
        key_map_(std::move(other.key_map_))
    {}

    template <typename InputIt>
    trie_map(
        InputIt first,
        InputIt last,
        key_mapper f = key_mapper(),
        allocator_type allocator = allocator_type()
    ) : size_(0), alloc_(std::move(allocator)), key_map_(std::move(f)) {
        init();
        for (auto cur = first; cur != last; ++cur) insert(*cur);
    }

    trie_map(
        std::initializer_list<value_type> init,
        key_mapper f = key_mapper(),
        allocator_type allocator = allocator_type()
    ) : trie_map(init.begin(), init.end(), std::move(f), std::move(allocator)) {}
    trie_map(std::initializer_list<value_type> init, allocator_type allocator) :
        trie_map(init.begin(), init.end(), key_mapper(), std::move(allocator))
    {}

    trie_map(const trie_map& other) :
        trie_map(
            other.begin(), other.end(), other.key_map_,
            alloc_traits::select_on_container_copy_construction(other.alloc_)
        )
    {}
    trie_map(const trie_map& other, allocator_type allocator) :
        trie_map(other.begin(), other.end(), std::move(allocator))
    {}

    ~trie_map() { root_.destroy(node_alloc_, link_alloc_); }

    trie_map& operator=(trie_map&& other) {
        base_ = std::move(other.base_);
        root_ = std::move(other.root_);
        size_ = std::move(other.size_);
        key_map_ = std::move(other.key_map_);

        // TODO reallocate all values if not
        if (alloc_traits::propagate_on_container_move_assignment::value) {
            alloc_ = std::move(other.alloc_);
        }
        // TODO reallocate all links if not
        if (link_alloc_traits::propagate_on_container_move_assignment::value) {
            link_alloc_ = std::move(other.link_alloc_);
        }
        // TODO reallocate all nodes if not
        if (node_alloc_traits::propagate_on_container_move_assignment::value) {
            node_alloc_ = std::move(other.node_alloc_);
        }
    }
    trie_map& operator=(const trie_map& other) {
        clear();
        insert(other.begin(), other.end());
        key_map_ = other.key_map_;

        if (alloc_traits::propagate_on_container_copy_assignment::value) {
            alloc_ = other.alloc_;
        }
        if (link_alloc_traits::propagate_on_container_copy_assignment::value) {
            link_alloc_ = other.link_alloc_;
        }
        if (node_alloc_traits::propagate_on_container_copy_assignment::value) {
            node_alloc_ = other.node_alloc_;
        }
    }

    mapped_type& operator[](const key_type& key) {
        iterator it = insert_handle(
            make_handle(std::piecewise_construct, std::tuple<const key_type&>(key), std::tuple<>())
        );
        return it->second;
    }

    mapped_type& at(const key_type& key) {
        iterator it = find(key);
        if (it == end()) throw std::out_of_range("rmr::trie_map::at");
        return it->second;
    }
    const mapped_type& at(const key_type& key) const {
        const_iterator it = find(key);
        if (it == end()) throw std::out_of_range("rmr::trie_map::at");
        return it->second;
    }

    template <typename LinkT>
    class generic_iterator {
    public:
        using difference_type   = std::ptrdiff_t;
        using value_type        = typename trie_map::value_type;
        using reference         = typename trie_map::reference;
        using pointer           = typename trie_map::pointer;
        using iterator_category = std::forward_iterator_tag;

        generic_iterator() : link_(nullptr) {}
        generic_iterator(LinkT* node) : link_(std::move(node)) {}
        generic_iterator(const generic_iterator&) = default;
        generic_iterator& operator=(const generic_iterator&) = default;

        template <typename = std::enable_if_t<std::is_const_v<LinkT>>>
        generic_iterator(const generic_iterator<std::remove_const_t<LinkT>>& other) :
            link_(const_cast<const LinkT*>(other.link_))
        {}

        generic_iterator& operator++() {
            do {
                *this = next(std::move(*this));
            } while(this->link_->handle == nullptr && this->link_->parent_index != R_END);
            return *this;
        }

        generic_iterator operator++(int) {
            generic_iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        reference operator*() const {
            return *(link_->handle->value_);
        }

        pointer operator->() const {
            return link_->handle->value_;
        }

        bool operator==(const generic_iterator& other) const {
            return (link_ == other.link_);
        }

        bool operator!=(const generic_iterator& other) const {
            return !(*this == other);
        }

        friend void swap(generic_iterator& lhs, generic_iterator& rhs) {
            std::swap(lhs.link_, rhs.link_);
        }

    private:
        LinkT* link_;

        static std::pair<generic_iterator, bool>
        step_down(generic_iterator it) {
            for (size_type pos = 0; pos < R; ++pos) {
                if (it.link_->children[pos] != nullptr) {
                    it.link_ = it.link_->children[pos];
                    return {it, true};
                }
            }
            return {it, false};
        }

        static std::pair<generic_iterator, bool>
        step_right(generic_iterator it) {
            if (it.link_->parent_index == R_END) return {it, false};

            for (size_type pos = it.link_->parent_index + 1; pos < R; ++pos) {
                link_type* parent = it.link_->parent;
                if (parent->children[pos] != nullptr) {
                    it.link_ = parent->children[pos];
                    return {it, true};
                }
            }
            return {it, false};
        }

        static std::pair<generic_iterator, bool>
        step_up(generic_iterator it) {
            it.link_ = it.link_->parent;
            return step_right(it);
        }

        static generic_iterator next(generic_iterator it) {
            bool stepped;

            std::tie(it, stepped) = step_down(std::move(it));
            if (stepped) return it;
            return skip(std::move(it));
        }
        static generic_iterator skip(generic_iterator it) {
            bool stepped;

            std::tie(it, stepped) = step_right(std::move(it));
            if (stepped) return it;

            while (it.link_->parent_index != R_END) {
                std::tie(it, stepped) = step_up(std::move(it));
                if (stepped) return it;
            }
            return it;
        }

        generic_iterator<std::remove_const_t<LinkT>> remove_const() const {
            generic_iterator<std::remove_const_t<LinkT>> it(
                const_cast<std::remove_const_t<LinkT>*>(link_)
            );
            return it;
        }

        friend class trie_map;
    };

    using iterator = generic_iterator<link_type>;
    using const_iterator = generic_iterator<const link_type>;

    iterator begin() {
        if (empty()) return end();
        iterator it(&base_);
        return ++it;
    }
    const_iterator begin() const { return cbegin(); }
    const_iterator cbegin() const {
        if (empty()) return cend();
        const_iterator it(&base_);
        return ++it;
    }

    iterator end() { return cend().remove_const(); }
    const_iterator end() const { return cend(); }
    const_iterator cend() const { return const_iterator(&base_); }

    size_type size() const { return size_; }
    bool empty() const { return size() == 0; }
    void clear() { root_.destroy(node_alloc_, link_alloc_); size_ = 0; }

    size_type count(const key_type& key) const {
        try {
            at(key);
            return 1;
        } catch (std::out_of_range&) {
            return 0;
        }
    }

    iterator find(const key_type& key) {
        return const_cast<const trie_map&>(*this).find(key).remove_const();
    }
    const_iterator find(const key_type& key) const {
        return find_key(root_iterator(), key.begin(), key.end());
    }

    template <typename... Args>
    std::pair<iterator, bool> emplace(Args&&... args) {
        node_type* np = make_handle(std::forward<Args>(args)...);

        iterator it = find(np->key());
        if (it != end()) {
            node_alloc_traits::deallocate(node_alloc_, np, 1);
            return {it, false};
        }

        return {insert_handle(np), true};
    }
    template <typename... Args>
    std::pair<iterator, bool> try_emplace(const key_type& key, Args&&... args) {
        iterator it = find(key);
        if (it == end()) return emplace(value_type(
            std::piecewise_construct,
            std::forward_as_tuple(key),
            std::forward_as_tuple(args...)
        ));
        else return {it, false};
    }
    template <typename... Args>
    std::pair<iterator, bool> try_emplace(key_type&& key, Args&&... args) {
        iterator it = find(key);
        if (it == end()) return emplace(value_type(
            std::piecewise_construct,
            std::forward_as_tuple(std::move(key)),
            std::forward_as_tuple(args...)
        ));
        else return {it, false};
    }

    struct insert_return_type {
        iterator  position;
        bool      inserted;
        node_type node;
    };

    std::pair<iterator, bool> insert(const value_type& value) {
        if (iterator it = find(value.first); it != end()) return {it, false};

        return {insert_handle(make_handle(value)), true};
    }

    iterator insert(const_iterator hint, const value_type& value) {
        if (iterator it = find(value.first); it != end()) return it;

        return insert_handle(hint, make_handle(value));
    }

    std::pair<iterator, bool> insert(value_type&& value) {
        if (iterator it = find(value.first); it != end()) return {it, false};

        return {insert_handle(make_handle(std::move(value))), true};
    }

    iterator insert(const_iterator hint, value_type&& value) {
        if (iterator it = find(value.first); it != end()) return it;

        return insert_handle(hint, make_handle(std::move(value)));
    }

    insert_return_type insert(node_type&& nh) {
        assert(nh.get_allocator() == node_alloc_);

        insert_return_type ret{};
        if (nh.empty()) {
            ret.position = end();
            ret.inserted = false;
            return ret;
        }

        auto [it, inserted] = insert(nh.value());
        if (inserted) {
            ret.position = it;
            nh.value_ = nullptr;
            ret.inserted = true;
        } else {
            ret.node = std::move(nh);
            ret.position = it;
            ret.inserted = false;
        }
        return ret;
    }

    iterator insert(const_iterator hint, node_type&& nh);

    template <typename InputIt>
    void insert(InputIt first, InputIt last) {
        for (auto it = first; it != last; ++it) insert(*it);
    }
    void insert(std::initializer_list<value_type> init) {
        insert(init.begin(), init.end());
    }
    std::pair<iterator, bool> insert_or_assign(const value_type& value) {
        auto [it, inserted] = insert(value);
        if (!inserted) it->second = value.second;
        return {it, inserted};
    }

    inline bool operator==(const trie_map& other) const {
        return size() == other.size() && std::equal(begin(), end(), other.begin());
    }
    inline bool operator<(const trie_map& other) const {
        return std::lexicographical_compare(begin(), end(), other.begin(), other.end());
    }
    inline bool operator!=(const trie_map& other) const {
        return !(*this == other);
    }
    inline bool operator>(const trie_map& other) const {
        return other < *this;
    }
    inline bool operator<=(const trie_map& other) const {
        return !(other < *this);
    }
    inline bool operator>=(const trie_map& other) const {
        return !(*this < other);
    }

    void swap(trie_map& other) {
        using std::swap;

        swap(base_, other.base_);
        swap(root_, other.root_);
        swap(key_map_, other.key_map_);
        if (alloc_traits::propagate_on_container_swap::value) {
            swap(alloc_, other.alloc_);
        }
        if (link_alloc_traits::propagate_on_container_swap::value) {
            swap(link_alloc_, other.link_alloc_);
        }
        if (node_alloc_traits::propagate_on_container_swap::value) {
            swap(node_alloc_, other.node_alloc_);
        }
    }
    friend void swap(trie_map& lhs, trie_map& rhs) {
        lhs.swap(rhs);
    }

    node_type extract(const_iterator pos) {
        node_type handle = std::move(*pos.link_->handle);
        erase(pos);
        return std::move(handle);
    }
    node_type extract(const key_type& key) {
        return extract(const_iterator(find(key)));
    }

    iterator erase(const_iterator pos) {
        return erase(pos.remove_const());
    }

    iterator erase(iterator pos) {
        size_type child = pos.link_->parent_index;
        iterator ret = std::next(pos);

        while (pos.link_->parent != &base_ && children_count(pos.link_->parent) == 1) {
            pos.link_ = pos.link_->parent;
            child = pos.link_->parent_index;
        }

        auto& parent = pos.link_->parent;
        pos.link_->destroy(node_alloc_, link_alloc_);
        if (pos.link_ != &root_) {
            parent->children[child] = nullptr;
            link_alloc_traits::deallocate(link_alloc_, pos.link_, 1);
        }

        --size_;
        return ret;
    }
    iterator erase(const_iterator first, const_iterator last) {
        for (auto it = first; it != last; ++it) erase(it);
        iterator ret(const_cast<link_type*>(last.link_));
        return ret;
    }
    size_type erase(const key_type& key) {
        iterator it = find(key);
        if (it == end()) return 0;
        erase(it);
        return 1;
    }

    template <typename KeyMapper2>
    void merge(trie_map<T, R, KeyMapper2, Key, Allocator>& source) {
        for (auto i = source.begin(), last = source.end(); i != last;) {
            auto pos = i++;
            if (find(pos->first) != end()) continue;
            insert(source.extract(pos));
        }
    }
    template <typename KeyMapper2>
    void merge(trie_map<T, R, KeyMapper2, Key, Allocator>&& source) {
        merge(source);
    }

    iterator longest_match(const key_type& key) {
        return const_cast<const trie_map&>(*this).longest_match(key).remove_const();
    }
    const_iterator longest_match(const key_type& key) const {
        return find_longest_match(root_iterator(), key.begin(), key.end());
    }

    std::pair<iterator, iterator>
    prefixed_with(const key_type& key) {
        auto [first, last] = const_cast<const trie_map&>(*this).prefixed_with(key);
        return {first.remove_const(), last.remove_const()};
    }
    std::pair<const_iterator, const_iterator>
    prefixed_with(const key_type& key) const {
        auto first = find_key(root_iterator(), key.begin(), key.end());
        if (first == end()) return { end(), end() };

        auto last = const_iterator::skip(first);
        return {++first, last};
    }

    allocator_type get_allocator() const { return alloc_; }
    key_mapper     key_map()       const { return key_map_; }

    static constexpr size_type radix() { return R; }

private:
    link_type            base_;
    link_type            root_;
    size_type            size_;
    allocator_type       alloc_;
    link_allocator_type  link_alloc_;
    node_allocator_type  node_alloc_;
    key_mapper           key_map_;

    void init() {
        base_.children[0] = &root_;
        base_.parent_index = R_END;

        root_.parent = &base_;
        root_.parent_index = 0;
    }

    const_iterator root_iterator() const {
        return const_iterator(&root_);
    }

    template <typename... Args>
    node_type* make_handle(Args&&... args) {
        node_type* handle = node_alloc_traits::allocate(node_alloc_, 1);
        node_alloc_traits::construct(node_alloc_, handle, node_alloc_);
        handle->value_ = alloc_traits::allocate(alloc_, 1);
        alloc_traits::construct(alloc_, handle->value_, std::forward<Args>(args)...);
        return handle;
    }

    link_type* make_link_type(link_type* parent, size_type parent_index) {
        link_type* link = link_alloc_traits::allocate(link_alloc_, 1);
        link_alloc_traits::construct(link_alloc_, link);
        link->parent = parent;
        link->parent_index = parent_index;
        return link;
    }

    iterator insert_handle(node_type* np) {
        return insert_handle(root_iterator(), 0, np);
    }

    iterator insert_handle(const_iterator _hint, node_type* np) {
        iterator hint = _hint.remove_const();
        size_type rank = 0;
        link_type* cur = hint.link_;
        while (cur != &root_) { ++rank; cur = cur->parent; }

        return insert_handle(_hint, rank, np);
    }

    iterator insert_handle(const_iterator _hint, size_type rank, node_type* np) {
        iterator hint = _hint.remove_const();

        iterator ret;
        insert_handle_impl(
            hint.link_,
            hint.link_->parent,
            hint.link_->parent_index,
            np->key(),
            rank,
            np,
            ret
        );
        return ret;
    }

    link_type* insert_handle_impl(
        link_type* root,
        link_type* parent,
        size_type parent_index,
        const key_type& key,
        size_type index,
        node_type* np,
        iterator& ret
    ) {
        if (root == nullptr) root = make_link_type(parent, parent_index);

        if (index == key.size()) {
            if (root->handle == nullptr) { root->handle = np; ++size_; }
            else                         node_alloc_traits::deallocate(node_alloc_, np, 1);
            ret = iterator(root);
        } else {
            size_type parent_index = key_map_(key[index]);
            auto& child = root->children[parent_index];
            child = insert_handle_impl(child, root, parent_index, key, index + 1, np, ret);
        }

        return root;
    }

    const_iterator next_iterator_for_char(
        const_iterator it, const char_type& c
    ) const {
        size_type index = key_map_(c);
        it.link_ = it.link_->children[index];
        return it;
    }

    const_iterator find_key(
        const_iterator it,
        typename key_type::const_iterator cur,
        typename key_type::const_iterator last
    ) const {
        if (it.link_ == nullptr) return end();
        if (cur == last)         return it;
        it = next_iterator_for_char(it, *cur);

        return find_key(std::move(it), ++cur, last);
    }

    const_iterator find_longest_match_candidate(
        const_iterator it, const_iterator prev,
        typename key_type::const_iterator cur,
        typename key_type::const_iterator last
    ) const {
        if (it.link_ == nullptr) return prev;
        if (cur == last)         return it;
        prev = it;
        it = next_iterator_for_char(it, *cur);

        return find_longest_match_candidate(std::move(it), std::move(prev), ++cur, last);
    }

    const_iterator find_longest_match(
        const_iterator it,
        typename key_type::const_iterator cur,
        typename key_type::const_iterator last
    ) const {
        auto pos = find_longest_match_candidate(it, end(), cur, last);
        while (pos.link_->handle == nullptr && pos.link_->parent_index != R_END)
            pos.link_ = pos.link_->parent;
        return pos;
    }

    static size_t children_count(const link_type* root) {
        size_t count = 0;
        for (auto& child : root->children) count += (child != nullptr);
        return count;
    }
};

} // namespace rmr
