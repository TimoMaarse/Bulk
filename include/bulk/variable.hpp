#pragma once

#include <bulk/future.hpp>
#include <bulk/world.hpp>
#include <memory>

/**
 * \file variable.hpp
 *
 * This header defines a distributed variable, which has a value on each
 * processor.
 */

namespace bulk {

template <typename T>
class future;

/**
 * Represents a distributed object with an image for each processor, that is
 * readable and writable from remote processors.
 */
template <typename T>
class var {
  public:
    class image {
      public:
        /**
         * Assign a value to a remote image
         *
         * \param value the new value of the image
         */
        var<T>& operator=(const T& value) {
            var_.impl_->put(t_, value);
            return var_;
        }

        /**
         * Get a future to the remote image value.
         */
        future<T> get() const { return var_.impl_->get(t_); }

      private:
        friend var;

        image(var<T>& v, int t) : var_(v), t_(t) {}

        var<T>& var_;
        int t_;
    };

    using value_type = T;

    /**
     * Initialize and registers the variable with the world
     */
    template <
        typename = std::enable_if_t<std::is_trivially_constructible<T>::value>>
    var(bulk::world& world) {
        // TODO: Here we should ask world to create the appropriate
        // subclass of var_impl
        // var_impl constructor can include a barrier in certain backends
        impl_ = std::make_unique<var_impl>(world);
    }

    /**
     * Initialize and registers the variable with the world, and sets its value
     * to `value`.
     */
    var(bulk::world& world, T value) : var(world) { *this = value; }

    /**
     * Deconstructs and deregisters the variable with the world
     */
    ~var() {
        // It could be that some core is already unregistering while
        // another core is still reading from the variable. Therefore
        // use a barrier before var_impl unregisters

        // FIXME we really do not want this on distributed, for obvious
        // performance reasons
        // FIXME: what if the variable has moved, do we delay moving until next
        // superstep?

        if (impl_)
            impl_->world_.barrier();
    }

    // A variable can not be copied
    var(var<T>& other) = delete;
    void operator=(var<T>& other) = delete;

    /**
      * Move from one var to another
      */
    var(var<T>&& other) { impl_ = std::move(other.impl_); }

    /**
     * Move from one var to another
     */
    void operator=(var<T>&& other) { impl_ = std::move(other.impl_); }

    /**
     * Get an image object to a remote image, added for syntactic sugar
     *
     * \returns a `var::image` object to the image with index `t`.
     */
    image operator()(int t) { return image(*this, t); };

    /**
     * Broadcast a value to all images.
     */
    void broadcast(T x) {
        for (int t = 0; t < world().active_processors(); ++t) {
            impl_->put(t, x);
        }
    }

    /**
     * Implicitly get the value held by the local image of the var
     *
     * \note This is for code like `myint = myvar + 5;`.
     */
    operator T&() { return impl_->value_; }
    operator const T&() const { return impl_->value_; }

    /**
     * Write to the local image
     *
     * \note This is for code like `myvar = 5;`.
     */
    var<T>& operator=(const T& rhs) {
        impl_->value_ = rhs;
        return *this;
    }

    /**
     * Returns the value held by the local image of the var
     *
     * \returns a reference to the value held by the local image
     */
    T& value() { return impl_->value_; }

    /**
     * Retrieve the world to which this var is registed.
     *
     * \returns a reference to the world of the var
     */
    bulk::world& world() { return impl_->world_; }

  private:
    // Default implementation is a value, world and id.
    // Backends can subclass bulk::var<T>::var_impl to add more.
    // Backends can overload var_impl::put and var_impl::get.
    class var_impl {
      public:
        var_impl(bulk::world& world) : world_(world), value_{} {
            // register_location_ can include a barrier in certain backends
            id_ = world.register_location_(&value_, sizeof(T));
        }
        virtual ~var_impl() { world_.unregister_location_(id_); }

        // No copies or moves
        var_impl(var_impl& other) = delete;
        var_impl(var_impl&& other) = delete;
        void operator=(var_impl& other) = delete;
        void operator=(var_impl&& other) = delete;

        virtual void put(int processor, const T& source) {
            world_.put_(processor, &source, sizeof(T), id_);
        }
        virtual future<T> get(int processor) const {
            future<T> result(world_);
            world_.get_(processor, id_, sizeof(T), &result.value());
            return result;
        }

        bulk::world& world_;
        T value_;
        int id_;
    };
    std::unique_ptr<var_impl> impl_;

    friend class image;
};

} // namespace bulk