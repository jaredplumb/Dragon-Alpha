/**
 * @file EngineNode.cpp
 * @brief Node graph lifecycle, draw traversal, and event dispatch implementation.
 */
#include "EngineNode.h"
#include <vector>

// The default rect for a node is at location 0,0 with a width and height equal to the system's design rect.
// All nodes are ultimately relative to the system's design rect.

ENode::ENode ()
:	_ref(ESystem::GetUniqueRef())
,	_rect(ESystem::GetDesignRect().OffsetToZero())
,	_alloc(false)
,	_visible(true)
,	_active(true)
,	_blocking(false)
,	_blocked(false)
,	_exit(false)
,	_parent(nullptr)
{
}

ENode::~ENode () {
	while(!_children.empty()) {
		if(_children.front()->_alloc)
			_AUTO_DELETE_LIST.push_back(_children.front());
		RemoveNode(*_children.front());
	}
	if(_parent)
		_parent->RemoveNode(*this);
}

int ENode::GetUniqueRef () const {
	return _ref;
}

ENode* ENode::GetParent() const {
	return _parent;
}

int ENode::GetWidth () const {
	return _rect.width;
}

int ENode::GetHeight () const {
	return _rect.height;
}

int ENode::GetX () const {
	return _rect.x;
}

int ENode::GetY () const {
	return _rect.y;
}

ERect ENode::GetRect() const {
	return _rect;
}

ERect ENode::GetParentRect () const {
	return _parent ? (ERect){-_rect.x, -_rect.y, _parent->_rect.width, _parent->_rect.height} : GetDesignRect();
}

ERect ENode::GetScreenRect () const {
	ERect screen = ESystem::GetScreenRect();
	for(const ENode* node = this; node != nullptr; node = node->_parent) {
		screen.x -= node->_rect.x;
		screen.y -= node->_rect.y;
	}
	return screen;
}

ERect ENode::GetSafeRect () const {
	ERect safe = ESystem::GetSafeRect();
	for(const ENode* node = this; node != nullptr; node = node->_parent) {
		safe.x -= node->_rect.x;
		safe.y -= node->_rect.y;
	}
	return safe;
}

ERect ENode::GetDesignRect () const {
	ERect rect = ESystem::GetDesignRect();
	for(const ENode* node = this; node != nullptr; node = node->_parent) {
		rect.x -= node->_rect.x;
		rect.y -= node->_rect.y;
	}
	return rect;
}

void ENode::SetRect (const ERect& rect) {
	_rect = rect;
}

void ENode::SetRectCenterInParent () {
	ERect parent = _parent ? _parent->_rect : ESystem::GetDesignRect();
	_rect.x = parent.width / 2 - _rect.width / 2;
	_rect.y = parent.height / 2 - _rect.height / 2;
}

void ENode::SetPosition (const EPoint& pos) {
	_rect.x = pos.x;
	_rect.y = pos.y;
}

void ENode::SetVisible (bool visible) {
	_visible = visible;
}

void ENode::SetActive (bool active) {
	_active = active;
}

void ENode::SetBlocking (bool blocking) {
	_blocking = blocking;
	if(_blocking)
		SetNodeAsFront();
}

void ENode::SetNodeAsFront () {
	if(_parent) {
		if(!_parent->_children.empty() && _parent->_children.front() != this) {
			auto i = std::find(_parent->_children.begin(), _parent->_children.end(), this);
			if(i != _parent->_children.end())
				_parent->_children.splice(_parent->_children.begin(), _parent->_children, i);
		}
		_parent->SetNodeAsFront();
	} else {
		if(!_ROOT_NODE_LIST.empty() && _ROOT_NODE_LIST.front() != this) {
			auto i = std::find(_ROOT_NODE_LIST.begin(), _ROOT_NODE_LIST.end(), this);
			if(i != _ROOT_NODE_LIST.end())
				_ROOT_NODE_LIST.splice(_ROOT_NODE_LIST.begin(), _ROOT_NODE_LIST, i);
		}
	}
}

bool ENode::IsVisible () const {
	return _visible;
}

bool ENode::IsActive () const {
	return _active;
}

bool ENode::IsBlocking () const {
	return _blocking;
}

ENode* ENode::NewNode (const EString& name) {
	auto i = _FACTORY_MAP.find((const char*)name);
	if(i != _FACTORY_MAP.end())
		return i->second();
	ESystem::Debug("Node factory map does not contain \"%s\"!\n", (const char*)name);
	return nullptr;
}

void ENode::RunNewNode (const EString& name) {
	// If this node is exiting, do nothing to prevent many calls to remove this node
	if(_exit)
		return;
	
	// If the ENode factory does not exist for the given name, do nothing
	if(_FACTORY_MAP.find((const char*)name) == _FACTORY_MAP.end()) {
		ESystem::Debug("Node factory map does not contain \"%s\"!\n", (const char*)name);
		return;
	}
	
	// Only the root node needs an the exit flag set because all children will be checked if an exit should happen and then all children will also be removed, this happends in the SendDraw function
	ENode* parent = this;
	while(parent->_parent)
		parent = parent->_parent;
	parent->_exit = true;
	parent->_next = name;
}

void ENode::RunNewNodeAsChild (const EString& name) {
	ENode* node = NewNode(name);
	if(node) {
		node->_alloc = true;
		node->_parent = this;
		_children.push_front(node);
	}
}

void ENode::ExitNode () {
	_exit = true; // This node (and consequently all it's children) will exit next time SendDraw is called
}

void ENode::ExitNodeCancel() {
	if(_parent) {
		_parent->ExitNodeCancel();
	} else {
		_exit = false;
		_next.Delete();
	}
}

void ENode::AddNode (ENode& node) {
	if(node._parent)
		node._parent->RemoveNode(node);
	node._parent = this;
	_children.push_front(&node);
}

void ENode::RemoveNode (ENode& node) {
	_children.remove(&node);
	node._parent = nullptr;
}

void ENode::SendDraw () {
	if(_visible) {
		ERect design = GetDesignRect();
		ESystem::MatrixSetModelDefault();
		ESystem::MatrixTranslateModel((float)-design.x, (float)-design.y);
		ESystem::MatrixSetProjectionDefault();
		ESystem::MatrixUpdate();
		OnDraw();
		std::vector<ENode*> drawChildren(_children.rbegin(), _children.rend());
		for(ENode* child : drawChildren)
			if(child && child->_parent == this)
				child->SendDraw();
	}
	
	if(_exit && SendExit()) {
		if(!_next.IsEmpty())
			_AUTO_RUN_LIST.emplace_back((const char*)_next);
		if(_parent)
			_parent->RemoveNode(*this);
		if(_alloc)
			_AUTO_DELETE_LIST.push_back(this);
	}
}

void ENode::SendTouch (int x, int y) {
	if(_visible && _active) {
		_blocked = false;
		std::vector<ENode*> children(_children.begin(), _children.end());
		for(ENode* child : children) {
			if(child == nullptr || child->_parent != this)
				continue;
			child->SendTouch(x - child->_rect.x, y - child->_rect.y);
			if(child->_parent == this && child->_visible && child->_active && (child->_blocking || child->_blocked)) {
				_blocked = true;
				return;
			}
		}
		OnTouch(x, y);
	}
}

void ENode::SendTouchUp (int x, int y) {
	if(_visible && _active) {
		_blocked = false;
		std::vector<ENode*> children(_children.begin(), _children.end());
		for(ENode* child : children) {
			if(child == nullptr || child->_parent != this)
				continue;
			child->SendTouchUp(x - child->_rect.x, y - child->_rect.y);
			if(child->_parent == this && child->_visible && child->_active && (child->_blocking || child->_blocked)) {
				_blocked = true;
				return;
			}
		}
		OnTouchUp(x, y);
	}
}

void ENode::SendTouchMove (int x, int y) {
	if(_visible && _active) {
		_blocked = false;
		std::vector<ENode*> children(_children.begin(), _children.end());
		for(ENode* child : children) {
			if(child == nullptr || child->_parent != this)
				continue;
			child->SendTouchMove(x - child->_rect.x, y - child->_rect.y);
			if(child->_parent == this && child->_visible && child->_active && (child->_blocking || child->_blocked)) {
				_blocked = true;
				return;
			}
		}
		OnTouchMove(x, y);
	}
}

void ENode::SendEvent (ENode* node) {
	if(_visible && _active) {
		_blocked = false;
		std::vector<ENode*> children(_children.begin(), _children.end());
		for(ENode* child : children) {
			if(child == nullptr || child->_parent != this)
				continue;
			child->SendEvent(node);
			if(child->_parent == this && child->_visible && child->_active && (child->_blocking || child->_blocked)) {
				_blocked = true;
				return;
			}
		}
		OnEvent(node);
	}
}

bool ENode::SendExit () {
	// All children must agree on the exit or SendExit returns false and no more checking occurs
	std::vector<ENode*> children(_children.begin(), _children.end());
	for(ENode* child : children)
		if(child != nullptr && child->_parent == this && !child->SendExit())
			return false;
	return OnExit();
}

void ENode::_DrawCallback () {
	const int64_t nowMilliseconds = ESystem::GetMilliseconds();
	++_FRAMES;
	if(_FRAMES <= 1) {
		_MILLISECONDS = nowMilliseconds;
		_ELAPSE = 1;
	}
	else {
		const int64_t elapsed = nowMilliseconds - _MILLISECONDS;
		_MILLISECONDS = nowMilliseconds;
		_ELAPSE = elapsed > 0 ? elapsed : 1;
	}
	
	while(!_AUTO_RUN_LIST.empty()) {
		ESystem::Debug("----------------------------------------------------------------\n");
		ESystem::Debug("%s\n", _AUTO_RUN_LIST.front().c_str());
		ESystem::Debug("----------------------------------------------------------------\n");
		ENode* node = NewNode(_AUTO_RUN_LIST.front().c_str());
		if(node) {
			node->_alloc = true;
			_ROOT_NODE_LIST.push_back(node);
		}
		_AUTO_RUN_LIST.erase(_AUTO_RUN_LIST.begin());
	}
	
	for(auto i = _ROOT_NODE_LIST.begin(); i != _ROOT_NODE_LIST.end(); i++)
		(*i)->SendDraw();
	
	while(!_AUTO_DELETE_LIST.empty()) {
		_AUTO_DELETE_LIST.unique();
		ENode* node = _AUTO_DELETE_LIST.front();
		auto i = std::find(_ROOT_NODE_LIST.begin(), _ROOT_NODE_LIST.end(), node);
		if(i != _ROOT_NODE_LIST.end())
			_ROOT_NODE_LIST.erase(i);
		delete node;
		_AUTO_DELETE_LIST.erase(_AUTO_DELETE_LIST.begin());
	}
}
