/**
 * @file UITransition.cpp
 * @brief Screen transition rendering and fade timing implementation.
 */
#include "UITransition.h"

void UITransition::New (eTransition transition, ENode* parent) {
	_transition = transition;
	switch(_transition) {
		case NONE:
			break;
		case FADE_BLACK:
		case FADE_IN_BLACK:
		case FADE_OUT_BLACK:
			_image.New(EColor::BLACK);
			break;
	}
	if(parent)
		parent->AddNode(*this);
}

void UITransition::OnDraw () {
	SetNodeAsFront();
	switch(_transition) {
		case NONE:
			return;
		case FADE_BLACK:
			if(_timer <= 0)
				_timer = GetMilliseconds();
			if(_timer > 0) {
				float alpha = 1.0f - (float)(GetMilliseconds() - _timer) / (float)FADE_TIME;
				if(alpha < 0.0f) {
					_timer = 0;
					_transition = FADE_OUT_BLACK;
					return;
				}
				_image.Draw(GetScreenRect(), alpha);
			}
			break;
		case FADE_IN_BLACK:
			if(_timer <= 0)
				_timer = GetMilliseconds();
			if(_timer > 0) {
				float alpha = 1.0f - (float)(GetMilliseconds() - _timer) / (float)FADE_TIME;
				if(alpha < 0.0f) {
					_transition = NONE;
					return;
				}
				_image.Draw(GetScreenRect(), alpha);
			}
			break;
		case FADE_OUT_BLACK:
			if(_timer > 0) {
				float alpha = (float)(GetMilliseconds() - _timer) / (float)FADE_TIME;
				if(alpha > 1.0f)
					alpha = 1.0f;
				_image.Draw(GetScreenRect(), alpha);
			}
			break;
	}
}

bool UITransition::OnExit () {
	switch(_transition) {
		case NONE:
			return true;
		case FADE_BLACK:
			// This transition is handled by FADE_OUT_BLACK
			break;
		case FADE_IN_BLACK:
			return true;
		case FADE_OUT_BLACK:
			if(_timer <= 0)
				_timer = GetMilliseconds();
			return GetMilliseconds() - _timer >= FADE_TIME;
	}
	return true;
}
