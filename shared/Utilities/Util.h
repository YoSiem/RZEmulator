#pragma once
/*
 * Copyright (C) 2011-2017 Project SkyFire <http://www.projectskyfire.org/>
 * Copyright (C) 2008-2017 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2017 MaNGOS <https://www.getmangos.eu/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <algorithm>
#include <cctype>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "Define.h"
#include "Errors.h"

class Tokenizer {
public:
    typedef std::vector<char const *> StorageType;

    typedef StorageType::size_type size_type;

    typedef StorageType::const_iterator const_iterator;
    typedef StorageType::reference reference;
    typedef StorageType::const_reference const_reference;

public:
    Tokenizer(const std::string &src, char const sep, uint32_t vectorReserve = 0);

    ~Tokenizer() { delete[] m_str; }

    const_iterator begin() const { return m_storage.begin(); }

    const_iterator end() const { return m_storage.end(); }

    size_type size() const { return m_storage.size(); }

    reference operator[](size_type i) { return m_storage[i]; }

    const_reference operator[](size_type i) const { return m_storage[i]; }

private:
    char *m_str;
    StorageType m_storage;
};

void stripLineInvisibleChars(std::string &src);

int64_t MoneyStringToMoney(const std::string &moneyString);

std::string secsToTimeString(uint64_t timeInSecs, bool shortText = false, bool hoursOnly = false);
uint32_t TimeStringToSecs(const std::string &timestring);
std::string TimeToTimestampStr(time_t t);

// See here: http://www.martinbroadhurst.com/case-insensitive-string-comparison-in-c.html
struct iequal {
    bool operator()(int c1, int c2) const { return std::toupper(c1) == std::toupper(c2); }
};

inline bool iequals(const std::string &str1, const std::string &str2)
{
    return std::equal(str1.begin(), str1.end(), str2.begin(), iequal());
}

/* Return a random number in the range min..max; (max-min) must be smaller than 32768.*/
int32_t irand(int32_t min, int32_t max);

/* Return a random number in the range min..max (inclusive). For reliable results, the difference
 * between max and min should be less than RAND32_MAX.*/
uint32_t urand(uint32_t min, uint32_t max);

/* Return a random number in the range 0 .. RAND32_MAX.*/
int32_t rand32();

/* Return a random number in the range min..max*/
float frand(float min, float max);

/* Return a random double from 0.0 to 1.0 (exclusive). Floats support only 7 valid decimal digits.
 * A double supports up to 15 valid decimal digits and is used internally (RAND32_MAX has 10 digits).
 * With an FPU, there is usually no difference in performance between float and double.
 */
double rand_norm(void);

/* Return a random double from 0.0 to 99.9999999999999. Floats support only 7 valid decimal digits.
 * A double supports up to 15 valid decimal digits and is used internally (RAND32_MAX has 10 digits).
 * With an FPU, there is usually no difference in performance between float and double.
 */
double rand_chance(void);

/* Return true if a random roll fits in the specified chance (range 0-100).*/
inline bool roll_chance_f(float chance)
{
    return chance > rand_chance();
}

/* Return true if a random roll fits in the specified chance (range 0-100).*/
inline bool roll_chance_i(int chance)
{
    return chance > irand(0, 99);
}

inline void ApplyPercentModFloatVar(float &var, float val, bool apply)
{
    if (val == -100.0f) // prevent set var to zero
        val = -99.99f;
    var *= (apply ? (100.0f + val) / 100.0f : 100.0f / (100.0f + val));
}

// Percentage calculation
template<class T, class U>
inline T CalculatePct(T base, U pct)
{
    return T(base * static_cast<float>(pct) / 100.0f);
}

template<class T, class U>
inline T GetPct(T curr, U max)
{
    return T(curr / static_cast<float>(max) * 100);
};

template<class T, class U>
inline T AddPct(T &base, U pct)
{
    return base += CalculatePct(base, pct);
}

template<class T, class U>
inline T ApplyPct(T &base, U pct)
{
    return base = CalculatePct(base, pct);
}

template<class T>
inline T RoundToInterval(T &num, T floor, T ceil)
{
    return num = std::min(std::max(num, floor), ceil);
}

// UTF8 handling
bool Utf8toWStr(const std::string &utf8str, std::wstring &wstr);
// in wsize==max size of buffer, out wsize==real string size
bool Utf8toWStr(char const *utf8str, size_t csize, wchar_t *wstr, size_t &wsize);

inline bool Utf8toWStr(const std::string &utf8str, wchar_t *wstr, size_t &wsize)
{
    return Utf8toWStr(utf8str.c_str(), utf8str.size(), wstr, wsize);
}

bool WStrToUtf8(std::wstring wstr, std::string &utf8str);
// size==real string size
bool WStrToUtf8(wchar_t *wstr, size_t size, std::string &utf8str);

size_t utf8length(std::string &utf8str); // set string to "" if invalid utf8 sequence
void utf8truncate(std::string &utf8str, size_t len);
bool StringToBool(std::string const &str);

inline bool isBasicLatinCharacter(wchar_t wchar)
{
    if (wchar >= L'a' && wchar <= L'z') // LATIN SMALL LETTER A - LATIN SMALL LETTER Z
        return true;
    if (wchar >= L'A' && wchar <= L'Z') // LATIN CAPITAL LETTER A - LATIN CAPITAL LETTER Z
        return true;
    return false;
}

inline bool isExtendedLatinCharacter(wchar_t wchar)
{
    if (isBasicLatinCharacter(wchar))
        return true;
    if (wchar >= 0x00C0 && wchar <= 0x00D6) // LATIN CAPITAL LETTER A WITH GRAVE - LATIN CAPITAL LETTER O WITH DIAERESIS
        return true;
    if (wchar >= 0x00D8 && wchar <= 0x00DE) // LATIN CAPITAL LETTER O WITH STROKE - LATIN CAPITAL LETTER THORN
        return true;
    if (wchar == 0x00DF) // LATIN SMALL LETTER SHARP S
        return true;
    if (wchar >= 0x00E0 && wchar <= 0x00F6) // LATIN SMALL LETTER A WITH GRAVE - LATIN SMALL LETTER O WITH DIAERESIS
        return true;
    if (wchar >= 0x00F8 && wchar <= 0x00FE) // LATIN SMALL LETTER O WITH STROKE - LATIN SMALL LETTER THORN
        return true;
    if (wchar >= 0x0100 && wchar <= 0x012F) // LATIN CAPITAL LETTER A WITH MACRON - LATIN SMALL LETTER I WITH OGONEK
        return true;
    if (wchar == 0x1E9E) // LATIN CAPITAL LETTER SHARP S
        return true;
    return false;
}

inline bool isCyrillicCharacter(wchar_t wchar)
{
    if (wchar >= 0x0410 && wchar <= 0x044F) // CYRILLIC CAPITAL LETTER A - CYRILLIC SMALL LETTER YA
        return true;
    if (wchar == 0x0401 || wchar == 0x0451) // CYRILLIC CAPITAL LETTER IO, CYRILLIC SMALL LETTER IO
        return true;
    return false;
}

inline bool isEastAsianCharacter(wchar_t wchar)
{
    if (wchar >= 0x1100 && wchar <= 0x11F9) // Hangul Jamo
        return true;
    if (wchar >= 0x3041 && wchar <= 0x30FF) // Hiragana + Katakana
        return true;
    if (wchar >= 0x3131 && wchar <= 0x318E) // Hangul Compatibility Jamo
        return true;
    if (wchar >= 0x31F0 && wchar <= 0x31FF) // Katakana Phonetic Ext.
        return true;
    if (wchar >= 0x3400 && wchar <= 0x4DB5) // CJK Ideographs Ext. A
        return true;
    if (wchar >= 0x4E00 && wchar <= 0x9FC3) // Unified CJK Ideographs
        return true;
    if (wchar >= 0xAC00 && wchar <= 0xD7A3) // Hangul Syllables
        return true;
    if (wchar >= 0xFF01 && wchar <= 0xFFEE) // Halfwidth forms
        return true;
    return false;
}

inline bool isNumeric(wchar_t wchar)
{
    return (wchar >= L'0' && wchar <= L'9');
}

inline bool isNumeric(char c)
{
    return (c >= '0' && c <= '9');
}

bool isMXNumeric(std::string);

inline bool isNumeric(char const *str)
{
    for (char const *c = str; *c; ++c)
        if (!isNumeric(*c))
            return false;

    return true;
}

inline bool isNumericOrSpace(wchar_t wchar)
{
    return isNumeric(wchar) || wchar == L' ';
}

inline bool isBasicLatinString(const std::wstring &wstr, bool numericOrSpace)
{
    for (size_t i = 0; i < wstr.size(); ++i)
        if (!isBasicLatinCharacter(wstr[i]) && (!numericOrSpace || !isNumericOrSpace(wstr[i])))
            return false;
    return true;
}

inline bool isExtendedLatinString(const std::wstring &wstr, bool numericOrSpace)
{
    for (size_t i = 0; i < wstr.size(); ++i)
        if (!isExtendedLatinCharacter(wstr[i]) && (!numericOrSpace || !isNumericOrSpace(wstr[i])))
            return false;
    return true;
}

inline bool isCyrillicString(const std::wstring &wstr, bool numericOrSpace)
{
    for (size_t i = 0; i < wstr.size(); ++i)
        if (!isCyrillicCharacter(wstr[i]) && (!numericOrSpace || !isNumericOrSpace(wstr[i])))
            return false;
    return true;
}

inline bool isEastAsianString(const std::wstring &wstr, bool numericOrSpace)
{
    for (size_t i = 0; i < wstr.size(); ++i)
        if (!isEastAsianCharacter(wstr[i]) && (!numericOrSpace || !isNumericOrSpace(wstr[i])))
            return false;
    return true;
}

inline wchar_t wcharToUpper(wchar_t wchar)
{
    if (wchar >= L'a' && wchar <= L'z') // LATIN SMALL LETTER A - LATIN SMALL LETTER Z
        return wchar_t(uint16_t(wchar) - 0x0020);
    if (wchar == 0x00DF) // LATIN SMALL LETTER SHARP S
        return wchar_t(0x1E9E);
    if (wchar >= 0x00E0 && wchar <= 0x00F6) // LATIN SMALL LETTER A WITH GRAVE - LATIN SMALL LETTER O WITH DIAERESIS
        return wchar_t(uint16_t(wchar) - 0x0020);
    if (wchar >= 0x00F8 && wchar <= 0x00FE) // LATIN SMALL LETTER O WITH STROKE - LATIN SMALL LETTER THORN
        return wchar_t(uint16_t(wchar) - 0x0020);
    if (wchar >= 0x0101 && wchar <= 0x012F) // LATIN SMALL LETTER A WITH MACRON - LATIN SMALL LETTER I WITH OGONEK (only %2=1)
    {
        if (wchar % 2 == 1)
            return wchar_t(uint16_t(wchar) - 0x0001);
    }
    if (wchar >= 0x0430 && wchar <= 0x044F) // CYRILLIC SMALL LETTER A - CYRILLIC SMALL LETTER YA
        return wchar_t(uint16_t(wchar) - 0x0020);
    if (wchar == 0x0451) // CYRILLIC SMALL LETTER IO
        return wchar_t(0x0401);

    return wchar;
}

inline wchar_t wcharToUpperOnlyLatin(wchar_t wchar)
{
    return isBasicLatinCharacter(wchar) ? wcharToUpper(wchar) : wchar;
}

inline wchar_t wcharToLower(wchar_t wchar)
{
    if (wchar >= L'A' && wchar <= L'Z') // LATIN CAPITAL LETTER A - LATIN CAPITAL LETTER Z
        return wchar_t(uint16_t(wchar) + 0x0020);
    if (wchar >= 0x00C0 && wchar <= 0x00D6) // LATIN CAPITAL LETTER A WITH GRAVE - LATIN CAPITAL LETTER O WITH DIAERESIS
        return wchar_t(uint16_t(wchar) + 0x0020);
    if (wchar >= 0x00D8 && wchar <= 0x00DE) // LATIN CAPITAL LETTER O WITH STROKE - LATIN CAPITAL LETTER THORN
        return wchar_t(uint16_t(wchar) + 0x0020);
    if (wchar >= 0x0100 && wchar <= 0x012E) // LATIN CAPITAL LETTER A WITH MACRON - LATIN CAPITAL LETTER I WITH OGONEK (only %2=0)
    {
        if (wchar % 2 == 0)
            return wchar_t(uint16_t(wchar) + 0x0001);
    }
    if (wchar == 0x1E9E) // LATIN CAPITAL LETTER SHARP S
        return wchar_t(0x00DF);
    if (wchar == 0x0401) // CYRILLIC CAPITAL LETTER IO
        return wchar_t(0x0451);
    if (wchar >= 0x0410 && wchar <= 0x042F) // CYRILLIC CAPITAL LETTER A - CYRILLIC CAPITAL LETTER YA
        return wchar_t(uint16_t(wchar) + 0x0020);

    return wchar;
}

inline void wstrToUpper(std::wstring &str)
{
    std::transform(str.begin(), str.end(), str.begin(), wcharToUpper);
}

inline void wstrToLower(std::wstring &str)
{
    std::transform(str.begin(), str.end(), str.begin(), wcharToLower);
}

struct tm *localtime_r(const time_t *time, struct tm *result);

bool utf8ToConsole(const std::string &utf8str, std::string &conStr);
bool consoleToUtf8(const std::string &conStr, std::string &utf8str);
bool Utf8FitTo(const std::string &str, std::wstring search);
void utf8printf(FILE *out, const char *str, ...);
void vutf8printf(FILE *out, const char *str, va_list *ap);

uint32_t CreatePIDFile(const std::string &filename);

std::string ByteArrayToHexStr(uint8_t const *bytes, uint32_t length, bool reverse = false);

void string_replace(std::string &str, const std::string &from, const std::string &to);

// simple class for not-modifyable list
template<typename T>
class HookList {
    typedef typename std::list<T>::iterator ListIterator;

private:
    typename std::list<T> m_list;

public:
    HookList<T> &operator+=(T t)
    {
        m_list.push_back(t);
        return *this;
    }

    HookList<T> &operator-=(T t)
    {
        m_list.remove(t);
        return *this;
    }

    size_t size() { return m_list.size(); }

    ListIterator begin() { return m_list.begin(); }

    ListIterator end() { return m_list.end(); }
};