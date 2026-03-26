/**
 * @file EngineTypes.h
 * @brief Shared primitive types and math/color/rect utilities for engine/runtime code.
 */
#ifndef ENGINE_TYPES_H_
#define ENGINE_TYPES_H_

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IOS
#define E_PLATFORM_IOS 1
#elif TARGET_OS_OSX
#define E_PLATFORM_MACINTOSH 1
#endif
#elif defined(__EMSCRIPTEN__)
#define E_PLATFORM_WEB 1
#elif defined(__MINGW32__) || defined(_MSC_VER)
#define E_PLATFORM_WINDOWS 1
#endif

using bool8_t = char;
using enum8_t = int8_t;
using enum16_t = int16_t;
using enum32_t = int32_t;
using enum64_t = int64_t;

#ifdef _DEBUG
#define E_ASSERT(x) { assert(x); }
#else
#define E_ASSERT(x) { (void)(x); }
#endif

#define E_MIN(x,y) ((x) < (y) ? (x) : (y))
#define E_MAX(x,y) ((x) > (y) ? (x) : (y))
#define E_ABS(x) ((x) < 0 ? ((x)*-1) : (x))

template <class T>
inline void E_SWAP (T& x, T& y) {
	T t = x;
	x = y;
	y = t;
}


class EColor {
public:
	/// RGBA format
	uint32_t color;
	
	inline EColor ()													: color(0) {}
	inline EColor (const EColor& c)										: color(c.color) {}
	inline EColor (uint32_t hex)										: color(hex) {}
	inline EColor (uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xff)	: color((a) | (b << 8) | (g << 16) | (r << 24)) {}
	inline EColor (float r, float g, float b, float a = 1.0f)			: color((uint8_t)(255.0f * a) | ((uint8_t)(255.0f * b) << 8) | ((uint8_t)(255.0f * g) << 16) | ((uint8_t)(255.0f * r) << 24)) {}
	inline uint8_t GetRed () const										{ return (uint8_t)((color & 0xff000000) >> 24); }
	inline uint8_t GetGreen () const									{ return (uint8_t)((color & 0x00ff0000) >> 16); }
	inline uint8_t GetBlue () const										{ return (uint8_t)((color & 0x0000ff00) >> 8); }
	inline uint8_t GetAlpha () const									{ return (uint8_t)(color & 0x000000ff); }
	inline float GetRedF () const										{ return (float)((uint8_t)((color & 0xff000000) >> 24) / 255.0f); }
	inline float GetGreenF () const										{ return (float)((uint8_t)((color & 0x00ff0000) >> 16) / 255.0f); }
	inline float GetBlueF () const										{ return (float)((uint8_t)((color & 0x0000ff00) >> 8) / 255.0f); }
	inline float GetAlphaF () const										{ return (float)((uint8_t)(color & 0x000000ff) / 255.0f); }
	inline void SetRed (uint8_t r)										{ color = (color & 0x00ffffff) | (r << 24); }
	inline void SetGreen (uint8_t g)									{ color = (color & 0xff00ffff) | (g << 16); }
	inline void SetBlue (uint8_t b)										{ color = (color & 0xffff00ff) | (b << 8); }
	inline void SetAlpha (uint8_t a)									{ color = (color & 0xffffff00) | (a); }
	inline bool operator== (const EColor& c) const						{ return color == c.color; }
	inline bool operator!= (const EColor& c) const						{ return color != c.color; }
	inline EColor& operator= (uint32_t hex)								{ color = hex; return *this; }
	
	static constexpr uint32_t BLACK = 0x000000ff;
	static constexpr uint32_t WHITE = 0xffffffff;
	static constexpr uint32_t CLEAR = 0x00000000;
	static constexpr uint32_t RED = 0xff0000ff;
	static constexpr uint32_t GREEN = 0x00ff00ff;
	static constexpr uint32_t BLUE = 0x0000ffff;
	static constexpr uint32_t CYAN = 0x00ffffff;
	static constexpr uint32_t MAGENTA = 0xff00ffff;
	static constexpr uint32_t YELLOW = 0xffff00ff;
	static constexpr uint32_t ALPHA = 0x000000ff;
};


class EPoint {
public:
	int x, y;
	
	inline EPoint ()											: x(0), y(0) {}
	inline EPoint (const EPoint& p)								: x(p.x), y(p.y) {}
	inline EPoint (int x_, int y_)								: x(x_), y(y_) {}
	inline EPoint& Set (const EPoint& p)						{ x = p.x; y = p.y; return *this; }
	inline EPoint& Set (int x_, int y_)							{ x = x_; y = y_; return *this; }
	inline EPoint& Offset (const EPoint& p)						{ x += p.x; y += p.y; return *this; }
	inline EPoint& Offset (int x_, int y_)						{ x += x_; y += y_; return *this; }
	inline bool operator== (const EPoint& p) const				{ return x == p.x && y == p.y; }
	inline bool operator!= (const EPoint& p) const				{ return x != p.x || y != p.y; }
	inline const EPoint operator+ (const EPoint& p) const		{ return EPoint(x + p.x, y + p.y); }
	inline const EPoint operator- () const						{ return EPoint(-x, -y); }
	inline const EPoint operator- (const EPoint& p) const		{ return EPoint(x - p.x, y - p.y); }
	inline const EPoint operator* (int t) const					{ return EPoint(x * t, y * t); }
	inline const EPoint operator/ (int t) const					{ return EPoint(x / t, y / t); }
	inline EPoint& operator= (const EPoint& p)					{ x = p.x; y = p.y; return *this; }
	inline EPoint& operator+= (const EPoint& p)					{ x += p.x; y += p.y; return *this; }
	inline EPoint& operator-= (const EPoint& p)					{ x -= p.x; y -= p.y; return *this; }
	inline EPoint& operator*= (int t)							{ x *= t; y *= t; return *this; }
	inline EPoint& operator/= (int t)							{ x /= t; y /= t; return *this; }
};


class ESize {
public:
	int width, height;
	
	inline ESize ()												: width(0), height(0) {}
	inline ESize (const ESize& s)								: width(s.width), height(s.height) {}
	inline ESize (int width_, int height_)						: width(width_), height(height_) {}
	inline ESize& Set (const ESize& s)							{ width = s.width; height = s.height; return *this; }
	inline ESize& Set (int width_, int height_)					{ width = width_; height = height_; return *this; }
	inline bool operator== (const ESize& s) const				{ return width == s.width && height == s.height; }
	inline bool operator!= (const ESize& s) const				{ return width != s.width || height != s.height; }
};


class EVector {
public:
	float x, y;
	
	inline EVector ()												: x((float)0), y((float)0) {}
	inline EVector (const EVector& p)								: x(p.x), y(p.y) {}
	inline EVector (float x_, float y_)								: x(x_), y(y_) {}
	inline bool operator== (const EVector& p) const					{ return x == p.x && y == p.y; }
	inline bool operator!= (const EVector& p) const					{ return x != p.x || y != p.y; }
	inline const EVector operator+ (const EVector& p) const			{ return EVector(x + p.x, y + p.y); }
	inline const EVector operator- () const							{ return EVector(-x, -y); }
	inline const EVector operator- (const EVector& p) const			{ return EVector(x - p.x, y - p.y); }
	inline const EVector operator* (float t) const					{ return EVector(x * t, y * t); }
	inline const EVector operator/ (float t) const					{ return EVector(x / t, y / t); }
	inline EVector& operator= (const EVector& p)					{ x = p.x; y = p.y; return *this; }
	inline EVector& operator+= (const EVector& p)					{ x += p.x; y += p.y; return *this; }
	inline EVector& operator-= (const EVector& p)					{ x -= p.x; y -= p.y; return *this; }
	inline EVector& operator*= (float t)							{ x *= t; y *= t; return *this; }
	inline EVector& operator/= (float t)							{ x /= t; y /= t; return *this; }
	inline float GetDistance (const EVector& v) const				{ return std::sqrt((x - v.x) * (x - v.x) + (y - v.y) * (y - v.y)); }
	inline float GetDistance2 (const EVector& v) const				{ return (x - v.x) * (x - v.x) + (y - v.y) * (y - v.y); }
	inline float GetDot (const EVector& v) const					{ return x * v.x + y * v.y; }
	inline float GetMagnitude () const								{ return std::sqrt(x * x + y * y); }
	inline float GetMagnitude2 () const								{ return x * x + y * y; } // For those times when you don't need the sqrt
	inline float GetLength () const									{ return std::sqrt(x * x + y * y); } // Yes, Magnitue and Length are the same
	inline EVector& Offset (const EVector& p)						{ x += p.x; y += p.y; return *this; }
	inline EVector& Offset (float x_, float y_)						{ x += x_; y += y_; return *this; }
	inline EVector& Normalize ()									{ float t = GetLength(); if(t) { x /= t; y /= t; } return *this; }
	inline EVector& Reflect (const EVector& n)						{ *this -= (n * ((float)2 * GetDot(n))); return *this; }
};


class ERect {
public:
	int x, y, width, height;
	
	inline ERect ()													: x(0), y(0), width(0), height(0) {}
	inline ERect (const ERect& r)									: x(r.x) , y(r.y), width(r.width), height(r.height) {}
	inline ERect (int x_, int y_, int w, int h)						: x(x_), y(y_), width(w), height(h) {}
	inline int GetLeft () const										{ return x; }
	inline int GetRight () const									{ return x + width; }
	inline int GetTop () const										{ return y; }
	inline int GetBottom () const									{ return y + height; }
	inline bool IsPointInRect (const EPoint& p) const				{ return p.x >= x && p.y >= y && p.x <= (x + width) && p.y <= (y + height); }
	inline bool IsPointInRect (int x_, int y_) const				{ return x_ >= x && y_ >= y && x_ <= (x + width) && y_ <= (y + height); }
	inline bool IsCollision (const ERect& r) const					{ return x < (r.x + r.width) && y < (r.y + r.height) && (x + width) > r.x && (y + height) > r.y; }
	inline bool IsCollision (int x_, int y_, int w, int h) const	{ return x < (x_ + w) && y < (y_ + h) && (x + width) > x_ && (y + height) > y_; }
	inline ERect& Set (int x_, int y_, int w, int h)				{ x = x_; y = y_; width = w; height = h; return *this; }
	inline ERect& SetLoc (const EPoint& p)							{ x = p.x; y = p.y; return *this; }
	inline ERect& SetLoc (int x_, int y_)							{ x = x_; y = y_; return *this; }
	inline ERect& SetSize (const EPoint& s)							{ width = s.x; height = s.y; return *this; }
	inline ERect& SetSize (int w, int h)							{ width = w; height = h; return *this; }
	inline ERect& Center (const ERect& r)							{ x = r.x + (r.width - width) / 2; y = r.y + (r.height - height) / 2; return *this; }
	inline ERect& Center (int x_, int y_, int w, int h)				{ x = x_ + (w - width) / 2; y = y_ + (h - height) / 2; return *this; }
	inline ERect& Offset (const EPoint& p)							{ x += p.x; y += p.y; return *this; }
	inline ERect& Offset (int x_, int y_)							{ x += x_; y += y_; return *this; }
	inline ERect& OffsetToZero ()									{ x = 0; y = 0; return *this; }
	inline bool operator== (const ERect& r) const					{ return x == r.x && y == r.y && width == r.width && height == r.height; }
	inline bool operator!= (const ERect& r) const					{ return x != r.x || y != r.y || width != r.width || height != r.height; }
};


class EMatrix32_4x4 {
public:
	float numbers[4][4];
	
	// GMatrix deliberately does not have constructors to avoid speed hits
	inline void SetIdentity ()																			{ *this = (EMatrix32_4x4){{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}}}; }
	inline void SetOrtho2D (float left, float right, float bottom, float top, float nearZ, float farZ)	{ *this = (EMatrix32_4x4){{{2.0f / (right - left), 0.0f, 0.0f, 0.0f}, {0.0f, 2.0f / (top - bottom), 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f / (farZ - nearZ), 0.0f}, {(left + right) / (left - right), (top + bottom) / (bottom - top), nearZ / (nearZ - farZ), 1.0f}}}; }
	inline void SetTranslation (float tx, float ty, float tz)											{ *this = (EMatrix32_4x4){{{numbers[0][0], numbers[0][1], numbers[0][2], numbers[0][3]}, {numbers[1][0], numbers[1][1], numbers[1][2], numbers[1][3]}, {numbers[2][0], numbers[2][1], numbers[2][2], numbers[2][3]}, {numbers[0][0] * tx + numbers[1][0] * ty + numbers[2][0] * tz + numbers[3][0], numbers[0][1] * tx + numbers[1][1] * ty + numbers[2][1] * tz + numbers[3][1], numbers[0][2] * tx + numbers[1][2] * ty + numbers[2][2] * tz + numbers[3][2], numbers[0][3] * tx + numbers[1][3] * ty + numbers[2][3] * tz + numbers[3][3]}}}; }
	inline void SetScale (float sx, float sy, float sz)													{ *this = (EMatrix32_4x4){{{numbers[0][0] * sx + numbers[1][0] * sy + numbers[2][0] * sz + numbers[3][0], numbers[0][1], numbers[0][2], numbers[0][3]}, {numbers[1][0], numbers[0][1] * sx + numbers[1][1] * sy + numbers[2][1] * sz + numbers[3][1], numbers[1][2], numbers[1][3]}, {numbers[2][0], numbers[2][1], numbers[0][2] * sx + numbers[1][2] * sy + numbers[2][2] * sz + numbers[3][2], numbers[2][3]}, {numbers[3][0], numbers[3][1], numbers[3][2], numbers[3][3]}}}; }
	inline void SetRotation (float radians)																{ float s = std::sin(radians); float c = std::cos(radians); *this = (EMatrix32_4x4){{{numbers[0][0] * c + numbers[0][1] * (-s), numbers[0][0] * s + numbers[0][1] * c, numbers[0][2], numbers[0][3]}, {numbers[1][0] * c + numbers[1][1] * (-s), numbers[1][0] * s + numbers[1][1] * c, numbers[1][2], numbers[1][3]}, {numbers[2][0] * c + numbers[2][1] * (-s), numbers[2][0] * s + numbers[2][1] * c, numbers[2][2], numbers[2][3]}, {numbers[3][0] * c + numbers[3][1] * (-s), numbers[3][0] * s + numbers[3][1] * c, numbers[3][2], numbers[3][3]}}}; }
	inline const EMatrix32_4x4 operator* (EMatrix32_4x4 t) const										{ EMatrix32_4x4 r; for(int i = 0; i < 4; i++) { r.numbers[i][0] = 0; r.numbers[i][1] = 0; r.numbers[i][2] = 0; r.numbers[i][3] = 0; for(int j = 0; j < 4; j++) { r.numbers[i][0] = numbers[j][0] * t.numbers[i][j] + r.numbers[i][0]; r.numbers[i][1] = numbers[j][1] * t.numbers[i][j] + r.numbers[i][1]; r.numbers[i][2] = numbers[j][2] * t.numbers[i][j] + r.numbers[i][2]; r.numbers[i][3] = numbers[j][3] * t.numbers[i][j] + r.numbers[i][3]; } } return r; }
};


class EString {
public:
	EString ();
	EString (const EString& string);
	EString (const char* string);
	~EString ();
	EString& New (const EString& string);
	EString& New (const char* string);
	void Delete ();
	EString& Format (const char* string, ...);
	int GetLength () const;
	bool IsEmpty () const; // Returns true if the string is NULL or ""
	void DeleteChar (uint32_t index);
	inline const char* String () const { return _string != nullptr ? _string : ""; } // Legacy alias.
	inline int Length () const { return GetLength(); } // Legacy alias.
	bool StartsWith (const EString& string) const;
	bool EndsWith (const EString& string) const;
	bool Contains (const EString& string) const;
	uint32_t Scan (const char* string, ...);
	EString& Add (const EString& string);
	EString& Add (const char* string);
	EString& ToLower ();
	EString& ToUpper ();
	EString& TrimSpaces ();
	inline EString& Trim () { return TrimSpaces(); } // Legacy alias.
	EString& TrimToHex ();
	EString& TrimToUnreserved ();
	EString& TrimExtension (); // Removes the extension off of a file path
	EString& TrimToDirectory (); // Removes the file off of a file path
	
	// Standard C string overrides with modifications
	static bool isalnum (char c);
	static bool isalpha (char c);
	static bool isdigit (char c);
	static bool isgraph (char c);
	static bool islower (char c);
	static bool isprint (char c);
	static bool ispunct (char c);
	static bool isspace (char c);
	static bool isupper (char c);
	static bool isxdigit (char c);
	static char* strcat (char* dst, const char* src);
	static int strcmp (const char* s1, const char* s2);
	static char* strcpy (char* dst, const char* src);
	static int stricmp (const char* s1, const char* s2);
	static char* stristr (const char* s, const char* find);
	static int strlen (const char* s);
	static char* strncat (char* dst, const char* src, int len);
	static int strncmp (const char* s1, const char* s2, int len);
	static char* strncpy (char* dst, const char* src, int len);
	static int strnicmp (const char* s1, const char* s2, int len);
	static char* strnistr (const char* s, const char* find, int len);
	static char* strnstr (const char* s, const char* find, int len);
	static char* strstr (const char* s, const char* find);
	
	// Finds the first accurance of find in s, and returns a pointer to the first character after, or NULL if find failed
	static char* strnnext (const char* s, const char* find, int len);
	static char* strnext (const char* s, const char* find);
	static char* strninext (const char* s, const char* find, int len);
	static char* strinext (const char* s, const char* find);
	
	static int strtoi (const char* s, char** end, int base); // Returns an int_t of the string, with an optional end pointer and base
	// strtod https://opensource.apple.com/source/tcl/tcl-10/tcl/compat/strtod.c
	static char tolower (char c);
	static char* tolower (char* s);
	static char toupper (char c);
	static char* toupper (char* s);
	
	// Operator overloads
	inline operator char* (void)									{ static char empty = '\0'; _length = 0; return _string != nullptr ? _string : &empty; }
	inline operator const char* (void) const						{ *(const_cast<int*>(&_length)) = 0; return _string != nullptr ? _string : ""; }
	inline char& operator[] (int index)								{ static char empty = '\0'; _length = 0; return (_string != nullptr && index >= 0) ? _string[index] : empty; }
	inline const char& operator[] (int index) const					{ static const char empty = '\0'; return (_string != nullptr && index >= 0) ? _string[index] : empty; }
	inline EString& operator= (const EString& string)				{ return New(string); }
	inline EString& operator= (const char* string)					{ return New(string); }
	inline const EString operator+ (const EString& string) const	{ return EString(*this).Add(string); }
	inline const EString operator+ (const char* string) const		{ return EString(*this).Add(string); }
	inline EString& operator+= (const EString& string)				{ return Add(string); }
	inline EString& operator+= (const char* string)					{ return Add(string); }
	inline bool operator== (const EString& string) const			{ return strcmp(_string, string._string) == 0; }
	inline bool operator== (const char* string) const				{ return strcmp(_string, string) == 0; }
	inline bool operator!= (const EString& string) const			{ return strcmp(_string, string._string) != 0; }
	inline bool operator!= (const char* string) const				{ return strcmp(_string, string) != 0; }
	inline bool operator< (const EString& string) const				{ return strcmp(_string, string._string) < 0; }
	inline bool operator< (const char* string) const				{ return strcmp(_string, string) < 0; }
	inline bool operator<= (const EString& string) const			{ return strcmp(_string, string._string) <= 0; }
	inline bool operator<= (const char* string) const				{ return strcmp(_string, string) <= 0; }
	inline bool operator> (const EString& string) const				{ return strcmp(_string, string._string) > 0; }
	inline bool operator> (const char* string) const				{ return strcmp(_string, string) > 0; }
	inline bool operator>= (const EString& string) const			{ return strcmp(_string, string._string) >= 0; }
	inline bool operator>= (const char* string) const				{ return strcmp(_string, string) >= 0; }
	
	// This class provides literal string access for template parameters
	template <int N>
	struct Literal {
		constexpr Literal (const char (&s)[N])						{ std::copy_n(s, N, string); }
		inline operator const char* (void) const					{ return string; }
		char string[N];
	};
	
private:
	char* _string;
	int _length;
};

void G_DEBUG (const char* message, ...);
int G_ERROR (const char* message, ...);
int G_WARNING (const char* message, ...);
void G_CONSOLE (const char* message, ...);

static inline bool G_isalnum (char c) { return EString::isalnum(c); }
static inline bool G_isalpha (char c) { return EString::isalpha(c); }
static inline bool G_isdigit (char c) { return EString::isdigit(c); }
static inline bool G_isgraph (char c) { return EString::isgraph(c); }
static inline bool G_islower (char c) { return EString::islower(c); }
static inline bool G_isprint (char c) { return EString::isprint(c); }
static inline bool G_ispunct (char c) { return EString::ispunct(c); }
static inline bool G_isspace (char c) { return EString::isspace(c); }
static inline bool G_isupper (char c) { return EString::isupper(c); }
static inline bool G_isxdigit (char c) { return EString::isxdigit(c); }
static inline char* G_strcat (char* dst, const char* src) { return EString::strcat(dst, src); }
static inline int G_strcmp (const char* s1, const char* s2) { return EString::strcmp(s1, s2); }
static inline char* G_strcpy (char* dst, const char* src) { return EString::strcpy(dst, src); }
static inline int G_stricmp (const char* s1, const char* s2) { return EString::stricmp(s1, s2); }
static inline char* G_stristr (const char* s, const char* find) { return EString::stristr(s, find); }
static inline uint32_t G_strlen (const char* s) { return (uint32_t)EString::strlen(s); }
static inline char* G_strncat (char* dst, const char* src, int len) { return EString::strncat(dst, src, len); }
static inline int G_strncmp (const char* s1, const char* s2, int len) { return EString::strncmp(s1, s2, len); }
static inline char* G_strncpy (char* dst, const char* src, int len) { return EString::strncpy(dst, src, len); }
static inline int G_strnicmp (const char* s1, const char* s2, int len) { return EString::strnicmp(s1, s2, len); }
static inline char* G_strnistr (const char* s, const char* find, int len) { return EString::strnistr(s, find, len); }
static inline char* G_strnstr (const char* s, const char* find, int len) { return EString::strnstr(s, find, len); }
static inline char* G_strstr (const char* s, const char* find) { return EString::strstr(s, find); }
static inline char G_tolower (char c) { return EString::tolower(c); }
static inline char* G_tolower (char* s) { return EString::tolower(s); }
static inline char G_toupper (char c) { return EString::toupper(c); }
static inline char* G_toupper (char* s) { return EString::toupper(s); }


class EArchive {
public:
	static constexpr uint8_t VERSION = 4;
	static int64_t Compress (const void* srcBuffer, int64_t srcSize, void* dstBuffer, int64_t dstSize);
	static int64_t Decompress (const void* srcBuffer, int64_t srcSize, void* dstBuffer, int64_t dstSize);
	static int64_t GetBufferBounds (int64_t srcSize);
};


#endif // ENGINE_TYPES_H_
