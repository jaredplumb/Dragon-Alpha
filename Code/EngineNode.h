/**
 * @file EngineNode.h
 * @brief Node graph lifecycle, draw traversal, and event dispatch interfaces.
 */
#ifndef ENGINE_NODE_H_
#define ENGINE_NODE_H_

#include "EngineTypes.h"
#include "EngineSystem.h"
#include <cassert>
#include <algorithm>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

class ENode {
public:
	ENode ();										// The default rect is the design rect
	inline bool operator== (const ENode& n) const	{ return _ref == n._ref; }
	inline bool operator!= (const ENode& n) const	{ return _ref != n._ref; }
	virtual ~ENode ();
	
	int GetUniqueRef () const;						// Returns this ENode's unique reference
	ENode* GetParent () const;						// Returns this nodes parent
	int GetWidth () const;
	int GetHeight () const;
	int GetX () const;
	int GetY () const;
	ERect GetRect () const;							// Returns this node's rect (the default rect is at 0,0 with a width and height equal to the system's design rect, all node locations are relative to the system's design rect)
	ERect GetParentRect () const;					// Returns the parent's rect relative to this node (or the design rect if no parent)
	ERect GetScreenRect () const;					// Returns the screen rect relative to this node
	ERect GetSafeRect () const;						// Returns the safe rect relative to this node
	ERect GetDesignRect () const;					// Returns the design rect relative to this node
	
	void SetRect (const ERect& rect);				// Sets this node's rect
	void SetRectCenterInParent ();					// Sets this node's rect to be centered in the parent or the system's design rect if no parent
	void SetPosition (const EPoint& pos);			// Sets this node's x,y in the rect
	void SetVisible (bool visible);					// A non-visible node only receives and sends to children OnExit (default true)
	void SetActive (bool active);					// A non-active node only receives and sends to children OnDraw and OnExit (default true)
	void SetBlocking (bool blocking);				// Sets the node to blocking preventing interactive events from being passed further and automatically calls SetNodeAsFront to ensure the blocking happens (default false)
	void SetNodeAsFront ();							// Sets the node to draw last and receive events first
	
	bool IsVisible () const;
	bool IsActive () const;
	bool IsBlocking () const;
	
	// Factory registration is string-based by design. Prefer the template helpers below instead
	// of calling RegisterFactory/RegisterAutoRun directly from gameplay code.
	static ENode* NewNode (const EString& name);	// Find the factory by name and create a new node
	static bool RegisterFactory (const char* name, ENode* (* function) ()) {
		if(name == nullptr || *name == '\0' || function == nullptr)
			return false;
		_FACTORY_MAP[name] = function;
		return true;
	}
	static bool RegisterAutoRun (const char* name) {
		if(name == nullptr || *name == '\0')
			return false;
		_AUTO_RUN_LIST.push_back(name);
		return true;
	}
	static bool HasFactory (const EString& name) {
		return _FACTORY_MAP.find((const char*)name) != _FACTORY_MAP.end();
	}
	static bool SetAutoRun (const char* name) {
		if(name == nullptr || *name == '\0')
			return false;
		if(_FACTORY_MAP.find(name) == _FACTORY_MAP.end())
			return false;
		_AUTO_RUN_LIST.clear();
		_AUTO_RUN_LIST.push_back(name);
		return true;
	}
	void RunNewNode (const EString& name);			// Exits the current node line down to the root and runs a new root node, can be called from the root or child and will exit all the way to the root
	void RunNewNodeAsChild (const EString& name);	// Runs the node as a child of this node with managed memory, deleting child node when the parent is deleted
	void ExitNode ();								// Exits the current node as soon as possible (usually the next frame), this will not exit up to the parent but will still exit all this node's children
	void ExitNodeCancel ();							// If this node is marked to exit, this function will cancel that exit and mark this node for continued running
	void AddNode (ENode& node);						// Adds a node to the front of the this nodes children
	void RemoveNode (ENode& node);					// Removes a node from this nodes children
	
	virtual void OnDraw () {}
	virtual void OnTouch (int x, int y) {}
	virtual void OnTouchUp (int x, int y) {}
	virtual void OnTouchMove (int x, int y) {}
	virtual void OnEvent (ENode* node) {}
	virtual bool OnExit () { return true; }
	
	void SendDraw ();
	void SendTouch (int x, int y);
	void SendTouchUp (int x, int y);
	void SendTouchMove (int x, int y);
	void SendEvent (ENode* node);
	bool SendExit ();
	
	/// Returns frame locked milliseconds
	static inline int64_t GetMilliseconds () {
		return _MILLISECONDS;
	}
	
	/// Returns frame locked elapse time
	static inline int64_t GetElapse () {
		return _ELAPSE;
	}
	
	// This random generator is from the BSD random, which is from: "Random number generators: good ones are hard to find" Park and Miller, Communications of the ACM, vol. 31, no. 10, October 1988, p. 1195.
	/// Returns a value from 0 to one minus range, unless range is 0, then the maximum 2^30 size is used (1073741824)
	static inline uint32_t GetRandom (uint32_t range = 0) {
		_RANDOM_SEED = 16807 * (_RANDOM_SEED % 127773) - 2836 * (_RANDOM_SEED / 127773);
		if((int32_t)_RANDOM_SEED <= 0)
			_RANDOM_SEED += 0x7fffffff;
		return range ? _RANDOM_SEED % range : _RANDOM_SEED % 1073741824;
	}
	
	/// Returns a random float value equal to or greater than min and equal to or less than max
	static inline float GetRandom (float min, float max) {
		return ((float)GetRandom(1000000) / (float)999999) * (max - min) + min;
	}
	
	/// Returns the current global random seed
	static inline uint32_t GetRandomSeed () {
		return _RANDOM_SEED;
	}
	
	/// Sets the global random seed, if seed is 0, then a random time value is used
	static inline void SetRandomSeed (uint32_t seed = 0) {
		_RANDOM_SEED = seed ? seed : ((uint32_t)ESystem::GetMilliseconds() + 1);
	}
	
private:
	int					_ref;
	ERect				_rect;		// This is the rect relative to the parent
	bool				_alloc;		// If this node is allocated, then it should be deleted internally when the parent goes out of scope
	bool				_visible;
	bool				_active;
	bool				_blocking;
	bool				_blocked;	// This is set to true if the node had interactive events blocked during the last interactive event (root nodes are never set to blocked and will always send events)
	bool				_exit;		// This node will exit as soon as possible blocking interactive events until complete, only the root node needs to be triggered as exiting becaue this is handled in the system callbacks
	EString				_next;		// This is the name of the node to run when this node is deleted, only the root node needs to know what node will run next because this is handled in the system callbacks
	ENode*				_parent;
	std::list<ENode*>	_children;
	
	static inline std::list<ENode*>									_ROOT_NODE_LIST;
	static inline std::unordered_map<std::string, ENode* (*) ()>	_FACTORY_MAP;
	static inline std::list<std::string>							_AUTO_RUN_LIST;
	static inline std::list<ENode*>									_AUTO_DELETE_LIST;
	static inline int64_t											_FRAMES = 0;
	static inline int64_t											_MILLISECONDS = ESystem::GetMilliseconds();
	static inline int64_t											_ELAPSE = 1;
	static inline uint32_t											_RANDOM_SEED = (uint32_t)ESystem::GetMilliseconds() + 1;
	
	static void _DrawCallback ();
	static inline void _DispatchInputToRoots (int x, int y, void (ENode::* callback) (int, int)) {
		std::vector<ENode*> roots(_ROOT_NODE_LIST.begin(), _ROOT_NODE_LIST.end());
		for(ENode* root : roots) {
			if(root == nullptr)
				continue;
			auto i = std::find(_ROOT_NODE_LIST.begin(), _ROOT_NODE_LIST.end(), root);
			if(i == _ROOT_NODE_LIST.end())
				continue;
			(root->*callback)(x - root->_rect.x, y - root->_rect.y);
		}
	}
	
	static inline void _TouchCallback (int x, int y) {
		_DispatchInputToRoots(x, y, &ENode::SendTouch);
	}
	
	static inline void _TouchUpCallback (int x, int y) {
		_DispatchInputToRoots(x, y, &ENode::SendTouchUp);
	}
	
	static inline void _TouchMoveCallback (int x, int y) {
		_DispatchInputToRoots(x, y, &ENode::SendTouchMove);
	}
	
	struct _STATIC_CONSTRUCTOR { inline _STATIC_CONSTRUCTOR () {
		ESystem::NewDrawCallback(_DrawCallback);
		ESystem::NewTouchCallback(_TouchCallback);
		ESystem::NewTouchUpCallback(_TouchUpCallback);
		ESystem::NewTouchMoveCallback(_TouchMoveCallback);
	} }; static inline _STATIC_CONSTRUCTOR _CONSTRUCTOR;
	
protected:
	class _Factory {
	public:
		inline _Factory (const char* name, ENode* (*function) ()) {
			_FACTORY_MAP[name] = function;
		}
	};
	
	class _AutoRun {
	public:
		inline _AutoRun (const char* name) {
			_AUTO_RUN_LIST.push_back(name);
		}
	};
	
}; // class ENode


// These template classes use the curiously recurring template pattern
// https://en.wikipedia.org/wiki/Curiously_recurring_template_pattern
//
// Preferred scene-factory model:
// - Use ENodeWithName<T, "Name"> for named scene registration.
// - Use ENodeAutoRun<T, "Name"> when that named scene should also bootstrap automatically.


template <class T, const EString::Literal S>
class ENodeWithName : public ENode {
public:
	static inline ENode* _NewNodeFromTemplate () { return new T; }
	static inline std::unique_ptr<ENode::_Factory> _factory = std::unique_ptr<ENode::_Factory>(new ENode::_Factory(S, _NewNodeFromTemplate));
	inline ENodeWithName () { assert(_factory); } // This is a hack to force static initialization
};


template <class T, EString::Literal S>
class ENodeAutoRun : public ENodeWithName<T, S> {
public:
	static inline std::unique_ptr<ENode::_AutoRun> _auto = std::unique_ptr<ENode::_AutoRun>(new ENode::_AutoRun(S));
	inline ENodeAutoRun () { assert(_auto); } // This is a hack to force static initialization
};


#endif // ENGINE_NODE_H_
