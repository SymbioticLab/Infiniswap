#ifndef __BIT_ITERATOR_HPP__
#define __BIT_ITERATOR_HPP__

#include <cstddef>
#include <iterator>

using BitType = unsigned char;
static size_t const Bits = sizeof(BitType) * 8;

struct BitReference {
	BitType *	m_word;
	BitType		m_mask;

	explicit BitReference(BitType * word, BitType mask) noexcept
		: m_word(word), m_mask(mask)
	{ }

	BitReference() noexcept
		: m_word(nullptr), m_mask(0ul)
	{ }

	explicit operator bool() const noexcept
	{ return *m_word & m_mask; }

	BitReference & operator=(bool v) noexcept
	{
		if (v)
			*m_word |= m_mask;
		else
			*m_word &= ~m_mask;

		return *this;
	}

	BitReference & operator=(BitReference const & v) noexcept
	{ return *this = bool(v); }

	void flip() noexcept
	{ *m_word ^= m_mask; }
};

bool operator==(BitReference const & l, BitReference const & r) noexcept
{ return bool(l) == bool(r); }

bool operator==(BitReference const & l, bool r) noexcept
{ return bool(l) == r; }

bool operator==(bool l, BitReference const & r) noexcept
{ return r == l; }

bool operator!=(BitReference const & l, BitReference const & r) noexcept
{ return !(l == r); }

bool operator!=(BitReference const & l, bool r) noexcept
{ return !(l == r); }

bool operator!=(bool l, BitReference const & r) noexcept
{ return !(l == r); }

struct BitIteratorBase
	: public std::iterator<std::random_access_iterator_tag, bool> {

	BitType *	m_data;
	size_t		m_offset;

	explicit BitIteratorBase(BitType *ptr, size_t off) noexcept
		: m_data(ptr), m_offset(off)
	{ }

	void increment() noexcept
	{
		if (m_offset++ == Bits - 1) {
			++m_data;
			m_offset = 0;
		}
	}

	void decrement() noexcept
	{
		if (m_offset-- == 0) {
			--m_data;
			m_offset = Bits - 1;
		}
	}

	void advance(ptrdiff_t d) noexcept
	{
		ptrdiff_t n = m_offset + d;
		m_data += n / Bits;
		n = n % Bits;

		if (n < 0) {
			n += Bits;
			--m_data;
		}

		m_offset = static_cast<size_t>(n);
	}

	bool operator==(BitIteratorBase const & it) const noexcept
	{ return m_data == it.m_data && m_offset == it.m_offset; }

	bool operator<(BitIteratorBase const & it) const noexcept
	{
		return m_data < it.m_data
			|| (m_data == it.m_data && m_offset < it.m_offset);
	}

	bool operator!=(BitIteratorBase const & it) const noexcept
	{ return !(*this == it); }

	bool operator>(BitIteratorBase const & it) const noexcept
	{ return it < *this; }

	bool operator<=(BitIteratorBase const & it) const noexcept
	{ return !(it < *this); }

	bool operator>=(BitIteratorBase const & it) const noexcept
	{ return !(*this < it); }
};

ptrdiff_t operator-(BitIteratorBase const & l, BitIteratorBase const & r) noexcept
{ return Bits * (l.m_data - r.m_data) + l.m_offset - r.m_offset; }

struct BitIterator : public BitIteratorBase
{
	using reference = BitReference;
	using pointer = BitReference *;
	using iterator = BitIterator;

	BitIterator()
		: BitIteratorBase(nullptr, 0)
	{ }

	BitIterator(BitType *ptr, size_t off)
		: BitIteratorBase(ptr, off)
	{ }

	reference operator*() const noexcept
	{ return reference(m_data, 1ul << m_offset); }

	iterator & operator++() noexcept
	{
		increment();
		return *this;
	}

	iterator operator++(int) noexcept
	{
		iterator it = *this;
		increment();
		return it;
	}

	iterator & operator--() noexcept
	{
		decrement();
		return *this;
	}

	iterator operator--(int) noexcept
	{
		iterator it = *this;
		decrement();
		return it;
	}

	iterator & operator+=(ptrdiff_t d) noexcept
	{
		advance(d);
		return *this;
	}

	iterator & operator-=(ptrdiff_t d) noexcept
	{
		*this += -d;
		return *this;
	}

	iterator operator+(ptrdiff_t d) const noexcept
	{
		iterator it = *this;
		return it += d;
	}

	iterator operator-(ptrdiff_t d) const noexcept
	{
		iterator it = *this;
		return it -= d;
	}

	reference operator[](ptrdiff_t d) const noexcept
	{ return *(*this + d); }
};

BitIterator operator+(ptrdiff_t d, BitIterator const & it) noexcept
{ return it + d; }

struct BitConstIterator : public BitIteratorBase
{
	using reference = bool;
	using const_reference = bool;
	using pointer = bool const *;
	using const_iterator = BitConstIterator;

	BitConstIterator() noexcept
		: BitIteratorBase(nullptr, 0)
	{ }

	BitConstIterator(BitType *ptr, size_t off) noexcept
		: BitIteratorBase(ptr, off)
	{ }

	BitConstIterator(BitIterator const & it) noexcept
		: BitIteratorBase(it.m_data, it.m_offset)
	{ }

	const_reference operator*() const noexcept
	{ return const_reference(BitReference(m_data, 1 << m_offset)); }

	const_iterator & operator++() noexcept
	{
		increment();
		return *this;
	}

	const_iterator operator++(int) noexcept
	{
		const_iterator it = *this;
		increment();
		return it;
	}

	const_iterator & operator--() noexcept
	{
		decrement();
		return *this;
	}

	const_iterator operator--(int) noexcept
	{
		const_iterator it = *this;
		decrement();
		return it;
	}

	const_iterator & operator+=(ptrdiff_t d) noexcept
	{
		advance(d);
		return *this;
	}

	const_iterator & operator-=(ptrdiff_t d) noexcept
	{ return (*this += -d); }

	const_iterator operator+(ptrdiff_t d) const noexcept
	{
		const_iterator it = *this;
		return it += d;
	}

	const_iterator operator-(ptrdiff_t d) const noexcept
	{
		const_iterator it = *this;
		return it -= d;
	}

	const_reference operator[](ptrdiff_t d) const noexcept
	{ return *(*this + d); }
};

BitConstIterator operator+(ptrdiff_t d, BitConstIterator const & it) noexcept
{ return it + d; }

#endif /*__BIT_ITERATOR_HPP__*/
