/*
 * Copyright (C) 2017 ScyllaDB
 *
 */

/*
 * This file is part of Scylla.
 *
 * Scylla is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Scylla is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Scylla.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <unordered_map>
#include <iosfwd>
#include <string_view>

#include <boost/program_options.hpp>

#include <seastar/core/sstring.hh>
#include <seastar/core/future.hh>

#include "seastarx.hh"

namespace seastar { class file; }
namespace seastar::json { class json_return_type; }
namespace YAML { class Node; }

namespace utils {

namespace bpo = boost::program_options;

class config_type {
    std::string_view _name;
    std::function<json::json_return_type (const void*)> _to_json;
private:
    template <typename NativeType>
    std::function<json::json_return_type (const void*)> make_to_json(json::json_return_type (*func)(const NativeType&)) {
        return [func] (const void* value) {
            return func(*static_cast<const NativeType*>(value));
        };
    }
public:
    template <typename NativeType>
    config_type(std::string_view name, json::json_return_type (*to_json)(const NativeType&)) : _name(name), _to_json(make_to_json(to_json)) {}
    std::string_view name() const { return _name; }
    json::json_return_type to_json(const void* value) const;
};

template <typename T>
extern const config_type config_type_for;

class config_file {
public:
    typedef std::unordered_map<sstring, sstring> string_map;
    typedef std::vector<sstring> string_list;

    enum class value_status {
        Used,
        Unused,
        Invalid,
    };

    enum class config_source : uint8_t {
        None,
        SettingsFile,
        CommandLine
    };

    struct config_src {
        std::string_view _name, _desc;
        const config_type* _type;
    protected:
        virtual const void* current_value() const = 0;
    public:
        config_src(std::string_view name, const config_type* type, std::string_view desc)
            : _name(name)
            , _desc(desc)
            , _type(type)
        {}
        virtual ~config_src() {}

        const std::string_view & name() const {
            return _name;
        }
        const std::string_view & desc() const {
            return _desc;
        }
        std::string_view type_name() const {
            return _type->name();
        }

        virtual void add_command_line_option(
                        bpo::options_description_easy_init&, const std::string_view&,
                        const std::string_view&) = 0;
        virtual void set_value(const YAML::Node&) = 0;
        virtual value_status status() const = 0;
        virtual config_source source() const = 0;
        json::json_return_type value_as_json() const;
    };

    template<typename T>
    struct named_value : public config_src {
    private:
        friend class config;
        std::string_view _name, _desc;
        T _value = T();
        config_source _source = config_source::None;
        value_status _value_status;
    protected:
        virtual const void* current_value() const override {
            return &_value;
        }
    public:
        typedef T type;
        typedef named_value<T> MyType;

        named_value(config_file* file, std::string_view name, value_status vs, const T& t = T(), std::string_view desc = {},
                std::initializer_list<T> allowed_values = {})
            : config_src(name, &config_type_for<T>, desc)
            , _value(t)
            , _value_status(vs)
        {
            file->add(*this);
        }
        value_status status() const override {
            return _value_status;
        }
        config_source source() const override {
            return _source;
        }
        bool is_set() const {
            return _source > config_source::None;
        }
        MyType & operator()(const T& t) {
            _value = t;
            return *this;
        }
        MyType & operator()(T&& t, config_source src = config_source::None) {
            _value = std::move(t);
            if (src > config_source::None) {
                _source = src;
            }
            return *this;
        }
        const T& operator()() const {
            return _value;
        }
        T& operator()() {
            return _value;
        }

        void add_command_line_option(bpo::options_description_easy_init&,
                        const std::string_view&, const std::string_view&) override;
        void set_value(const YAML::Node&) override;
    };

    typedef std::reference_wrapper<config_src> cfg_ref;

    config_file(std::initializer_list<cfg_ref> = {});

    void add(cfg_ref);
    void add(std::initializer_list<cfg_ref>);
    void add(const std::vector<cfg_ref> &);

    boost::program_options::options_description get_options_description();
    boost::program_options::options_description get_options_description(boost::program_options::options_description);

    boost::program_options::options_description_easy_init&
    add_options(boost::program_options::options_description_easy_init&);

    /**
     * Default behaviour for yaml parser is to throw on
     * unknown stuff, invalid opts or conversion errors.
     *
     * Error handling function allows overriding this.
     *
     * error: <option name>, <message>, <optional value_status>
     *
     * The last arg, opt value_status will tell you the type of
     * error occurred. If not set, the option found does not exist.
     * If invalid, it is invalid. Otherwise, a parse error.
     *
     */
    using error_handler = std::function<void(const sstring&, const sstring&, std::optional<value_status>)>;

    void read_from_yaml(const sstring&, error_handler = {});
    void read_from_yaml(const char *, error_handler = {});
    future<> read_from_file(const sstring&, error_handler = {});
    future<> read_from_file(file, error_handler = {});

    using configs = std::vector<cfg_ref>;

    configs set_values() const;
    configs unset_values() const;
    const configs& values() const {
        return _cfgs;
    }
private:
    configs
        _cfgs;
};

extern template struct config_file::named_value<seastar::log_level>;

}

