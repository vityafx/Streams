#ifndef RUST_STREAMS_H
#define RUST_STREAMS_H

#if defined _MSC_VER
#include "Optional/optional.hpp"
# else
#include <experimental/optional>
#endif

namespace streams {

	template<typename T>
	using Optional = std::experimental::optional<T>;

	using std::experimental::nullopt;

	template <typename DerivedStreamExtractor>
	struct StreamExtractor {
		// TODO: noexcept?
		auto get() {
			return static_cast<DerivedStreamExtractor*>(this)->get_impl();
		}

		// TODO: noexcept?
		bool advance() {
			return static_cast<DerivedStreamExtractor*>(this)->advance_impl();
		}
	};

	template <typename IteratorType>
	struct SequenceStreamExtractor : StreamExtractor<SequenceStreamExtractor<IteratorType>> {
		SequenceStreamExtractor(IteratorType b, IteratorType e) : current(b), next(b), begin(b), end(e) {}

		IteratorType current;
		IteratorType next;
		const IteratorType begin;
		const IteratorType end;

		auto get_impl() {
			return current;
		}

		bool advance_impl() {
			if (next != end) {
				current = next++;
				return true;
			} else {
				return false;
			}
		}
	};

	// TODO: templated size_t counter + static_assertion?
	template<typename ExtractorType>
	struct SkipFirstStreamExtractor : StreamExtractor<SkipFirstStreamExtractor<ExtractorType>> {
		SkipFirstStreamExtractor(ExtractorType extractor, size_t count) : source(extractor), skipCount(count) {}

		ExtractorType source;
		size_t skipCount;

		auto get_impl() {
			return source.get();
		}

		bool advance_impl() {
			while (skipCount != 0) {
				--skipCount;
				if (!source.advance()) {
					return false;
				}
			}
			return source.advance();
		}

	};

	template<typename ExtractorType, typename Predicate>
	struct SkipWhileStreamExtractor : StreamExtractor<SkipWhileStreamExtractor<ExtractorType, Predicate>> {
		SkipWhileStreamExtractor(ExtractorType extractor, Predicate&& predicate) : source(extractor), predicate(std::forward<Predicate>(predicate)) {}

		ExtractorType source;
		Predicate predicate;
		bool skipping = true;

		auto get_impl() {
			return source.get();
		}

		bool advance_impl() {
			if (skipping) {
				while (skipping && source.advance()) {
					skipping = predicate(*source.get());
				}
				return !skipping; // depleted stream : skipping == true
			} else {
				return source.advance();
			}
		}

	};

	template<typename ExtractorType>
	struct TakeStreamExtractor : StreamExtractor<TakeStreamExtractor<ExtractorType>> {
		TakeStreamExtractor(ExtractorType extractor, size_t count) : source(extractor), limit(count) {}

		ExtractorType source;
		size_t limit;

		auto get_impl() {
			return source.get();
		}

		bool advance_impl() {
			if (limit != 0) {
				--limit;
				return source.advance();
			}
			return false;
		}

	};

	template<typename ExtractorType, typename Predicate>
	struct TakeWhileStreamExtractor : StreamExtractor<TakeWhileStreamExtractor<ExtractorType, Predicate>> {
		TakeWhileStreamExtractor(ExtractorType extractor, Predicate&& predicate) : source(extractor), predicate(std::forward<Predicate>(predicate)) {}

		ExtractorType source;
		Predicate predicate;
		bool taking = true;

		auto get_impl() {
			return source.get();
		}

		bool advance_impl() {
			taking &= taking && source.advance() && predicate(*source.get());
			return taking;
		}

	};


	template<typename ExtractorType, typename Predicate>
	struct FilterStreamExtractor : StreamExtractor<FilterStreamExtractor<ExtractorType, Predicate>> {
		FilterStreamExtractor(ExtractorType extractor, Predicate&& p) : source(extractor), predicate(std::forward<Predicate>(p)) {}

		ExtractorType source;
		Predicate predicate;

		auto get_impl() {
			return source.get();
		}

		bool advance_impl() {
			if (!source.advance()) {
				return false;
			}
			auto element = source.get();
			while (!predicate(*element)) {
				if (source.advance()) {
					element = source.get();
				} else {
					return false;
				}
			}
			return true;
		}

	};


	template<typename ExtractorType, typename MapFunc>
	struct MapStreamExtractor : StreamExtractor<MapStreamExtractor<ExtractorType, MapFunc>> {
		MapStreamExtractor(ExtractorType sourceExtractor, MapFunc&& m) : source(sourceExtractor), mapFunc(std::forward<MapFunc>(m)) {}

		ExtractorType source;
		MapFunc mapFunc;

		//using InputType = decltype(std::declval<ExtractorType>().get());
		//using OutputType = decltype(std::declval<Mapper>()(*std::declval<InputType>()))
		//OutputType value;

		// TODO: consider using shared_ptr here
        decltype(mapFunc(*std::declval<decltype(source.get())>())) value {};

		auto get_impl() {
			value = mapFunc(*source.get());
			return &value;
		}

		bool advance_impl() {
			return source.advance();
		}

	};


	template<typename ExtractorType, typename Inspector>
	struct InspectStreamExtractor : StreamExtractor<InspectStreamExtractor<ExtractorType, Inspector>> {
		InspectStreamExtractor(ExtractorType extractor, Inspector&& inspector) : source(extractor), inspector(std::forward<Inspector>(inspector)) {}

		ExtractorType source;
		Inspector inspector;

		auto get_impl() {
			auto value = source.get();
			inspector(*value);
			return value;
		}

		bool advance_impl() {
			return source.advance();
		}

	};


	// TODO: extractors are copied every time...
	// (n+1)th extractor will copy a chain of n extractors 
	template<typename ExtractorType>
	struct BaseStreamInterface {
		ExtractorType extractor;
		using value_type = typename std::remove_reference<decltype(*extractor.get())>::type;

		BaseStreamInterface(ExtractorType e) : extractor(e) {}

		// Intermediate Operations

		template<typename MapFunc>
		auto map(MapFunc&& mapper) {
			using Extractor = MapStreamExtractor<decltype(extractor), MapFunc>;
			return BaseStreamInterface<Extractor>(Extractor(extractor, std::forward<MapFunc>(mapper)));
		}

		template<typename Predicate>
		auto filter(Predicate&& predicate) {
			using Extractor = FilterStreamExtractor<decltype(extractor), Predicate>;
			return BaseStreamInterface<Extractor>(Extractor(extractor, std::forward<Predicate>(predicate)));
		}

		auto skip(size_t count) {
			using Extractor = SkipFirstStreamExtractor<decltype(extractor)>;
			return BaseStreamInterface<Extractor>(Extractor(extractor, count));
		}

		template<typename Predicate>
		auto skipWhile(Predicate&& predicate) {
			using Extractor = SkipWhileStreamExtractor<decltype(extractor), Predicate>;
			return BaseStreamInterface<Extractor>(Extractor(extractor, std::forward<Predicate>(predicate)));
		}

		auto take(size_t count) {
			using Extractor = TakeStreamExtractor<decltype(extractor)>;
			return BaseStreamInterface<Extractor>(Extractor(extractor, count));
		}

		template<typename Predicate>
		auto takeWhile(Predicate&& predicate) {
			using Extractor = TakeWhileStreamExtractor<decltype(extractor), Predicate>;
			return BaseStreamInterface<Extractor>(Extractor(extractor, std::forward<Predicate>(predicate)));
		}

		template<typename Inspector>
		auto inspect(Inspector&& inspector) {
			using Extractor = InspectStreamExtractor<decltype(extractor), Inspector>;
			return BaseStreamInterface<Extractor>(Extractor(extractor, std::forward<Inspector>(inspector)));
		}


		// Non-Terminal

		Optional<value_type> next() {
			if (extractor.advance()) {
				return{ *extractor.get() };
			}
			return{};
		}

		Optional<value_type> nth(size_t n) {
			while (n && extractor.advance()) {
				--n;
			}
			return next();
		}
		// Terminal Operations  

		template<typename Callable>
		void forEach(Callable&& callable) {
			while (extractor.advance()) {
				callable(*extractor.get());
			}
		}

		size_t count() {
			size_t counter = 0;
			while (extractor.advance()) {
				++counter;
			}
			return counter;
		}

		template<typename Predicate>
		bool any(Predicate&& predicate) {
			while (extractor.advance()) {
				if (predicate(*extractor.get())) {
					return true;
				}
			}
			return false;
		}

		template<typename Predicate>
		bool all(Predicate&& predicate) {
			while (extractor.advance()) {
				if (!predicate(*extractor.get())) {
					return false;
				}
			}
			return true;
		}

		template<typename Accumulator, typename Fold>
		Accumulator fold(Accumulator a, Fold&& fold) {
			while (extractor.advance()) {
				a = fold(a, *extractor.get());
			}
			return a;
		}

        template <template<class...> class Container = std::vector>
        auto collect() {
            Container<typename std::remove_const<value_type>::type> container;
            while (extractor.advance()) {
                container.push_back(*extractor.get());
            }
            return container;
        }

	};

	template<typename Container>
	auto from(const Container& container) {
		using Extractor = SequenceStreamExtractor<decltype(std::begin(container))>;
		return BaseStreamInterface<Extractor>(Extractor(std::begin(container), std::end(container)));
	}

	template<typename Container>
	auto from(const Container&& container) = delete; // currently disastrous

} // namespace streams

#endif // !RUST_STREAMS_H