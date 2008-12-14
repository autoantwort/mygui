/*!
	@file
	@author		Albert Semenov
	@date		11/2007
	@module
*/
#include "MyGUI_Gui.h"
#include "MyGUI_Widget.h"
#include "MyGUI_InputManager.h"
#include "MyGUI_SkinManager.h"
#include "MyGUI_SubWidgetManager.h"
#include "MyGUI_WidgetManager.h"
#include "MyGUI_WidgetSkinInfo.h"
#include "MyGUI_WidgetDefines.h"
#include "MyGUI_LayerKeeper.h"
#include "MyGUI_LayerItemKeeper.h"
#include "MyGUI_LayerItem.h"
#include "MyGUI_LayerManager.h"
#include "MyGUI_RenderItem.h"
#include "MyGUI_ISubWidget.h"
#include "MyGUI_ISubWidgetText.h"

namespace MyGUI
{

	//MYGUI_RTTI_BASE_IMPLEMENT2( Widget );

	const float WIDGET_TOOLTIP_TIMEOUT = 0.5f;

	Widget::Widget(WidgetType _behaviour, const IntCoord& _coord, Align _align, const WidgetSkinInfoPtr _info, WidgetPtr _parent, ICroppedRectangle * _croppedParent, IWidgetCreator * _creator, const std::string & _name) :
		ICroppedRectangle(IntCoord(_coord.point(), _info->getSize()), _align, _croppedParent), // размер по скину
		LayerItem(),
		UserData(),
		mStateInfo(_info->getStateInfo()),
		mMaskPeekInfo(_info->getMask()),
		mText(null),
		mMainSkin(null),
		mEnabled(true),
		mVisible(true),
		mInheritedShow(true),
		mAlpha(ALPHA_MIN),
		mInheritsAlpha(true),
		mName(_name),
		mTexture(_info->getTextureName()),
		mParent(_parent),
		mIWidgetCreator(_creator),
		mNeedKeyFocus(false),
		mNeedMouseFocus(true),
		mInheritsPeek(false),
		mWidgetClient(null),
		mNeedToolTip(false),
		mEnableToolTip(true),
		mToolTipVisible(false),
		mToolTipCurrentTime(0),
		mToolTipOldIndex(ITEM_NONE),
		mWidgetType(_behaviour)
	{

#ifdef _DEBUG
		// проверяем соответсвие входных данных
		if (mWidgetType == WidgetType::Child) {
			MYGUI_ASSERT(mCroppedParent, "must be cropped");
			MYGUI_ASSERT(mParent, "must be parent");
		}
		else if (mWidgetType == WidgetType::Overlapped) {
			MYGUI_ASSERT((mParent == null) == (mCroppedParent == null), "error cropped");
		}
		else if (mWidgetType == WidgetType::Popup) {
			MYGUI_ASSERT(!mCroppedParent, "cropped must be null");
			MYGUI_ASSERT(mParent, "must be parent");
		}
#endif

		// корректируем абсолютные координаты
		mAbsolutePosition = _coord.point();
		if (null != mCroppedParent) mAbsolutePosition += mCroppedParent->getAbsolutePosition();

		// имя отсылателя сообщений
		mWidgetEventSender = this;

		initialiseWidgetSkin(_info, _coord.size());

		// дочернее окно обыкновенное
		if (mWidgetType == WidgetType::Child) {
			// если есть леер, то атачимся
			LayerItemKeeper * layer_item = mParent->getLayerItemKeeper();
			if (layer_item) _attachToLayerItemKeeper(layer_item);
		}
		// дочернее нуно перекрывающееся
		else if (mWidgetType == WidgetType::Overlapped) {
			// дочернее перекрывающееся
			if (mParent) {
				LayerItemKeeper * layer_item = mParent->getLayerItemKeeper();
				if (layer_item) {
					LayerItemKeeper * child = layer_item->createItem();
					_attachToLayerItemKeeper(child);
				}
			}
		}

	}

	Widget::~Widget()
	{
		Gui::getInstance().eventFrameStart -= newDelegate(this, &Widget::frameEntered);

		if (mToolTipVisible) eventToolTip(this, ToolTipInfo(ToolTipInfo::Hide));

		shutdownWidgetSkin();

		_destroyAllChildWidget();
	}

	void Widget::changeWidgetSkin(const std::string& _skinname)
	{
		WidgetSkinInfoPtr skin_info = SkinManager::getInstance().getSkin(_skinname);
		baseChangeWidgetSkin(skin_info); 
	}

	void Widget::baseChangeWidgetSkin(WidgetSkinInfoPtr _info)
	{
		// FIXME
		// если есть клиент то удаляются детишки

		// актуально для рутовых
		std::string layername;
		LayerKeeper * layer = getLayerKeeper();
		if (layer) {
			layername = layer->getName();
			LayerManager::getInstance().detachFromLayerKeeper(this);
		}

		// актуально для WidgetType::Overlapped и WidgetType::Child
		LayerItemKeeper * layer_item = getLayerItemKeeper();

		IntSize size = mCoord.size();

		shutdownWidgetSkin();
		initialiseWidgetSkin(_info, size);

		// дочернее окно обыкновенное
		if (mWidgetType == WidgetType::Child) {
			// если есть леер, то атачимся
			if (layer_item) _attachToLayerItemKeeper(layer_item);
		}
		// дочернее оуно перекрывающееся
		else if (mWidgetType == WidgetType::Overlapped) {
			// дочернее перекрывающееся
			if (mParent) {
				if (layer_item) {
					_attachToLayerItemKeeper(layer_item);
				}
			}
			// перекрывающееся рутовое
			else {
				if (!layername.empty()) {
					LayerManager::getInstance().attachToLayerKeeper(layername, this);
				}
			}
		}
		else if (mWidgetType == WidgetType::Popup) {
			if (!layername.empty()) {
				LayerManager::getInstance().attachToLayerKeeper(layername, this);
			}
		}

	}

	void Widget::initialiseWidgetSkin(WidgetSkinInfoPtr _info, const IntSize& _size)
	{
		mTexture = _info->getTextureName();
		mStateInfo = _info->getStateInfo();
		setSize(_info->getSize());

		// загружаем кирпичики виджета
		SubWidgetManager & manager = SubWidgetManager::getInstance();
		for (VectorSubWidgetInfo::const_iterator iter=_info->getBasisInfo().begin(); iter!=_info->getBasisInfo().end(); ++iter) {

			ISubWidget * sub = manager.createSubWidget(*iter, this);
			mSubSkinChild.push_back(sub);

			// ищем дефолтные сабвиджеты
			if (mMainSkin == null) mMainSkin = sub->castType<ISubWidgetRect>(false);
			if (mText == null) mText = sub->castType<ISubWidgetText>(false);

		}

		if (false == isRootWidget()) {
			// проверяем наследуемую скрытость
			if ((!mParent->isShow()) || (!mParent->_isInheritedShow())) {
				mInheritedShow = false;

				for (VectorSubWidget::iterator skin = mSubSkinChild.begin(); skin != mSubSkinChild.end(); ++skin) (*skin)->hide();
				for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) (*widget)->hide();
			}
		}

		setState("normal");

		// парсим свойства
		const MapString & properties = _info->getProperties();
		if (false == properties.empty()) {
			MapString::const_iterator iter = properties.end();
			if ((iter = properties.find("FontName")) != properties.end()) setFontName(iter->second);
			if ((iter = properties.find("FontHeight")) != properties.end()) setFontHeight(utility::parseInt(iter->second));
			if ((iter = properties.find("NeedKey")) != properties.end()) setNeedKeyFocus(utility::parseBool(iter->second));
			if ((iter = properties.find("NeedMouse")) != properties.end()) setNeedMouseFocus(utility::parseBool(iter->second));
			if ((iter = properties.find("AlignText")) != properties.end()) setTextAlign(Align::parse(iter->second));
			if ((iter = properties.find("Colour")) != properties.end()) setColour(Colour::parse(iter->second));
			if ((iter = properties.find("Pointer")) != properties.end()) mPointer = iter->second;
			if ((iter = properties.find("Show")) != properties.end()) { utility::parseBool(iter->second) ? show() : hide(); }
		}

		// выставляем альфу, корректировка по отцу автоматически
		setAlpha(ALPHA_MAX);

		// создаем детей скина
		const VectorChildSkinInfo& child = _info->getChild();
		for (VectorChildSkinInfo::const_iterator iter=child.begin(); iter!=child.end(); ++iter) {
			WidgetPtr widget = createWidgetT(iter->type, iter->skin, iter->coord, iter->align);
			widget->_setInternalData(iter->name);
			// заполняем UserString пропертями
			for (MapString::const_iterator prop=iter->params.begin(); prop!=iter->params.end(); ++prop) {
				widget->setUserString(prop->first, prop->second);
			}
			// для детей скина свой список
			mWidgetChildSkin.push_back(widget);
			mWidgetChild.pop_back();
		}

		setSize(_size);
	}

	void Widget::shutdownWidgetSkin()
	{
		// позже сделать детач без текста
		_detachFromLayerItemKeeper();

		// удаляем все сабскины
		for (VectorSubWidget::iterator skin = mSubSkinChild.begin(); skin != mSubSkinChild.end(); ++skin) {
			delete (*skin);
		}
		mSubSkinChild.clear();
		mMainSkin = null;
		mText = null;
		mStateInfo.clear();

		// удаляем виджеты чтобы ли в скине
		for (VectorWidgetPtr::iterator iter=mWidgetChildSkin.begin(); iter!=mWidgetChildSkin.end(); ++iter) {
			// Добавляем себя чтобы удалилось
			mWidgetChild.push_back(*iter);
			_destroyChildWidget(*iter);
		}
		mWidgetChildSkin.clear();
	}

	WidgetPtr Widget::baseCreateWidget(WidgetType _behaviour, const std::string & _type, const std::string & _skin, const IntCoord& _coord, Align _align, const std::string & _layer, const std::string & _name)
	{
		WidgetPtr widget = WidgetManager::getInstance().createWidget(_behaviour, _type, _skin, _coord, _align, this,
			_behaviour == WidgetType::Popup ? null : this, this, _name);

		mWidgetChild.push_back(widget);
		// присоединяем виджет с уровню
		if (!_layer.empty()) LayerManager::getInstance().attachToLayerKeeper(_layer, widget);
		return widget;
	}

	WidgetPtr Widget::createWidgetRealT(const Ogre::String & _type, const Ogre::String & _skin, const FloatCoord& _coord, Align _align, const Ogre::String & _name)
	{
		return createWidgetT(_type, _skin, WidgetManager::getInstance().convertRelativeToInt(_coord, this), _align, _name);
	}

	void Widget::_setAlign(const IntCoord& _coord, bool _update)
	{
		// для виджета изменение х у  не меняються
		_setAlign(_coord.size(), _update);
	}

	void Widget::_setAlign(const IntSize& _size, bool _update)
	{
		if (!mCroppedParent) return;

		bool need_move = false;
		bool need_size = false;
		IntCoord coord = mCoord;

		// первоначальное выравнивание
		if (mAlign.isHStretch()) {
			// растягиваем
			coord.width = mCoord.width + (mCroppedParent->getWidth() - _size.width);
			need_size = true;
		}
		else if (mAlign.isRight()) {
			// двигаем по правому краю
			coord.left = mCoord.left + (mCroppedParent->getWidth() - _size.width);
			need_move = true;
		}
		else if (mAlign.isHCenter()) {
			// выравнивание по горизонтали без растяжения
			coord.left = (mCroppedParent->getWidth() - mCoord.width) / 2;
			need_move = true;
		}

		if (mAlign.isVStretch()) {
			// растягиваем
			coord.height = mCoord.height + (mCroppedParent->getHeight() - _size.height);
			need_size = true;
		}
		else if (mAlign.isBottom()) {
			// двигаем по нижнему краю
			coord.top = mCoord.top + (mCroppedParent->getHeight() - _size.height);
			need_move = true;
		}
		else if (mAlign.isVCenter()) {
			// выравнивание по вертикали без растяжения
			coord.top = (mCroppedParent->getHeight() - mCoord.height) / 2;
			need_move = true;
		}

		if (need_move) {
			if (need_size) setCoord(coord);
			else setPosition(coord.point());
		}
		else if (need_size) {
			setSize(coord.size());
		}
		else _updateView(); // только если не вызвано передвижение и сайз

	}

	void Widget::_updateView()
	{

		bool margin = mCroppedParent ? _checkMargin() : false;

		// вьюпорт стал битым
		if (margin) {

			// проверка на полный выход за границу
			if (_checkOutside()) {

				// запоминаем текущее состояние
				mIsMargin = margin;

				// скрываем
				_setVisible(false);

				// для тех кому нужно подправить себя при движении
				//for (VectorSubWidget::iterator skin = mSubSkinChild.begin(); skin != mSubSkinChild.end(); ++skin) (*skin)->_updateView();

				// вся иерархия должна быть проверенна
				for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) (*widget)->_updateView();
				for (VectorWidgetPtr::iterator widget = mWidgetChildSkin.begin(); widget != mWidgetChildSkin.end(); ++widget) (*widget)->_updateView();

				return;
			}

		}
		else if (false == mIsMargin) { // мы не обрезаны и были нормальные

			// запоминаем текущее состояние
			//mIsMargin = margin;

			//_setVisible(true);
			// для тех кому нужно подправить себя при движении
			for (VectorSubWidget::iterator skin = mSubSkinChild.begin(); skin != mSubSkinChild.end(); ++skin) (*skin)->_updateView();

			return;

		}

		// запоминаем текущее состояние
		mIsMargin = margin;

		// если скин был скрыт, то покажем
		_setVisible(true);

		// обновляем наших детей, а они уже решат обновлять ли своих детей
		for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) (*widget)->_updateView();
		for (VectorWidgetPtr::iterator widget = mWidgetChildSkin.begin(); widget != mWidgetChildSkin.end(); ++widget) (*widget)->_updateView();
		for (VectorSubWidget::iterator skin = mSubSkinChild.begin(); skin != mSubSkinChild.end(); ++skin) (*skin)->_updateView();

	}

	void Widget::setCaption(const Ogre::UTFString & _caption)
	{
		if (null != mText) mText->setCaption(_caption);
	}

	const Ogre::UTFString & Widget::getCaption()
	{
		if (null == mText) {
			static Ogre::UTFString empty;
			return empty;
		}
		return mText->getCaption();
	}

	void Widget::setTextAlign(Align _align)
	{
		if (mText != null) mText->setTextAlign(_align);
	}

	Align Widget::getTextAlign()
	{
		if (mText != null) return mText->getTextAlign();
		return Align::Default;
	}

	void Widget::setColour(const Colour& _colour)
	{
		if (null != mText) mText->setColour(_colour);
	}

	const Colour& Widget::getColour()
	{
		return (null == mText) ? Colour::Zero : mText->getColour();
	}

	void Widget::setFontName(const Ogre::String & _font)
	{
		if (null != mText) mText->setFontName(_font);
	}

	const std::string & Widget::getFontName()
	{
		if (null == mText) {
			static std::string empty;
			return empty;
		}
		return mText->getFontName();
	}

	void Widget::setFontHeight(uint16 _height)
	{
		if (null != mText) mText->setFontHeight(_height);
	}

	uint16 Widget::getFontHeight()
	{
		return (null == mText) ? 0 : mText->getFontHeight();
	}

	bool Widget::setState(const std::string & _state)
	{
		MapWidgetStateInfo::const_iterator iter = mStateInfo.find(_state);
		if (iter == mStateInfo.end()) return false;
		size_t index=0;
		for (VectorSubWidget::iterator skin = mSubSkinChild.begin(); skin != mSubSkinChild.end(); ++skin, ++index) {
			ISubWidget * info = (*skin);
			StateInfo * data = (*iter).second[index];
			if (data != null) info->_setStateData(data);
		}
		return true;
	}

	void Widget::setEnabled(bool _enabled)
	{
		mEnabled = _enabled;

		for (VectorWidgetPtr::iterator iter = mWidgetChild.begin(); iter != mWidgetChild.end(); ++iter) (*iter)->setEnabled(_enabled);
		for (VectorWidgetPtr::iterator iter = mWidgetChildSkin.begin(); iter != mWidgetChildSkin.end(); ++iter) (*iter)->setEnabled(_enabled);

		if (mEnabled) setState("normal");
		else {
			setState("disabled");
			InputManager::getInstance()._unlinkWidget(this);
		}
	}

	// удяляет неудачника
	void Widget::_destroyChildWidget(WidgetPtr _widget)
	{
		MYGUI_ASSERT(null != _widget, "invalid widget pointer");

		VectorWidgetPtr::iterator iter = std::find(mWidgetChild.begin(), mWidgetChild.end(), _widget);
		if (iter != mWidgetChild.end()) {

			// сохраняем указатель
			MyGUI::WidgetPtr widget = *iter;

			// удаляем из списка
			*iter = mWidgetChild.back();
			mWidgetChild.pop_back();

			// отписываем от всех
			WidgetManager::getInstance().unlinkFromUnlinkers(_widget);

			// непосредственное удаление
			_deleteWidget(widget);
		}
		else {
			MYGUI_EXCEPT("Widget '" << _widget->getName() << "' not found");
		}
	}

	// удаляет всех детей
	void Widget::_destroyAllChildWidget()
	{
		WidgetManager & manager = WidgetManager::getInstance();
		while (false == mWidgetChild.empty()) {

			// сразу себя отписывем, иначе вложенной удаление убивает все
			WidgetPtr widget = mWidgetChild.back();
			mWidgetChild.pop_back();

			//if (widget->isRootWidget()) widget->detachWidget();

			// отписываем от всех
			manager.unlinkFromUnlinkers(widget);

			// и сами удалим, так как его больше в списке нет
			delete widget;
		}
	}

	IntCoord Widget::getClientCoord()
	{
		if (mWidgetClient != null) return mWidgetClient->getCoord();
		return IntCoord(0, 0, mCoord.width, mCoord.height);
	}

	IntSize Widget::getTextSize()
	{
		return (null == mText) ? IntSize() : mText->getTextSize();
	}

	IntCoord Widget::getTextCoord()
	{
		return (null == mText) ? IntCoord() : mText->getCoord();
	}

	void Widget::setAlpha(float _alpha)
	{
		if (mAlpha == _alpha) return;
		mAlpha = _alpha;
		if (null != mParent) mRealAlpha = mAlpha * (mInheritsAlpha ? mParent->_getRealAlpha() : ALPHA_MAX);
		else mRealAlpha = mAlpha;

		for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) (*widget)->_updateAlpha();
		for (VectorWidgetPtr::iterator widget = mWidgetChildSkin.begin(); widget != mWidgetChildSkin.end(); ++widget) (*widget)->_updateAlpha();
		for (VectorSubWidget::iterator skin = mSubSkinChild.begin(); skin != mSubSkinChild.end(); ++skin) (*skin)->setAlpha(mRealAlpha);
	}

	void Widget::_updateAlpha()
	{
		MYGUI_DEBUG_ASSERT(null != mParent, "Widget must have parent");
		mRealAlpha = mAlpha * (mInheritsAlpha ? mParent->_getRealAlpha() : ALPHA_MAX);

		for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) (*widget)->_updateAlpha();
		for (VectorWidgetPtr::iterator widget = mWidgetChildSkin.begin(); widget != mWidgetChildSkin.end(); ++widget) (*widget)->_updateAlpha();
		for (VectorSubWidget::iterator skin = mSubSkinChild.begin(); skin != mSubSkinChild.end(); ++skin) (*skin)->setAlpha(mRealAlpha);
	}

	void Widget::setInheritsAlpha(bool _inherits)
	{
		mInheritsAlpha = _inherits;
		// принудительно обновляем
		float alpha = mAlpha;
		mAlpha = 101;
		setAlpha(alpha);
	}

	LayerItem * Widget::_findLayerItem(int _left, int _top)
	{
		// проверяем попадание
		if (!mVisible
			|| !mEnabled
			|| !mShow
			|| (!mNeedMouseFocus && !mInheritsPeek)
			|| !_checkPoint(_left, _top)
			// если есть маска, проверяем еще и по маске
			|| ((!mMaskPeekInfo->empty()) && (!mMaskPeekInfo->peek(IntPoint(_left - mCoord.left, _top - mCoord.top), mCoord))))
				return null;
		// спрашиваем у детишек
		for (VectorWidgetPtr::reverse_iterator widget= mWidgetChild.rbegin(); widget != mWidgetChild.rend(); ++widget) {
			// общаемся только с послушными детьми
			if ((*widget)->mWidgetType == WidgetType::Popup) continue;

			LayerItem * item = (*widget)->_findLayerItem(_left - mCoord.left, _top - mCoord.top);
			if (item != null) return item;
		}
		// спрашиваем у детишек скна
		for (VectorWidgetPtr::reverse_iterator widget= mWidgetChildSkin.rbegin(); widget != mWidgetChildSkin.rend(); ++widget) {
			LayerItem * item = (*widget)->_findLayerItem(_left - mCoord.left, _top - mCoord.top);
			if (item != null) return item;
		}
		// непослушные дети
		return mInheritsPeek ? null : this;
	}

	void Widget::_updateAbsolutePoint()
	{
		// мы рут, нам не надо
		if (!mCroppedParent) return;

		mAbsolutePosition = mCroppedParent->getAbsolutePosition() + mCoord.point();

		for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) (*widget)->_updateAbsolutePoint();
		for (VectorWidgetPtr::iterator widget = mWidgetChildSkin.begin(); widget != mWidgetChildSkin.end(); ++widget) (*widget)->_updateAbsolutePoint();
		for (VectorSubWidget::iterator skin = mSubSkinChild.begin(); skin != mSubSkinChild.end(); ++skin) (*skin)->_correctView();
	}

	void Widget::setPosition(const IntPoint & _point)
	{
		// обновляем абсолютные координаты
		mAbsolutePosition += _point - mCoord.point();

		for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) (*widget)->_updateAbsolutePoint();
		for (VectorWidgetPtr::iterator widget = mWidgetChildSkin.begin(); widget != mWidgetChildSkin.end(); ++widget) (*widget)->_updateAbsolutePoint();

		mCoord = _point;
		_updateView();
	}

	void Widget::setSize(const IntSize & _size)
	{
		// устанавливаем новую координату а старую пускаем в расчеты
		IntSize old = mCoord.size();
		mCoord = _size;

		bool show = true;

		// обновляем выравнивание
		bool margin = mCroppedParent ? _checkMargin() : false;

		if (margin) {
			// проверка на полный выход за границу
			if (_checkOutside()) {
				// скрываем
				show = false;
			}
		}

		_setVisible(show);

		// передаем старую координату , до вызова, текущая координата отца должна быть новой
		for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) (*widget)->_setAlign(old, mIsMargin || margin);
		for (VectorWidgetPtr::iterator widget = mWidgetChildSkin.begin(); widget != mWidgetChildSkin.end(); ++widget) (*widget)->_setAlign(old, mIsMargin || margin);
		for (VectorSubWidget::iterator skin = mSubSkinChild.begin(); skin != mSubSkinChild.end(); ++skin) (*skin)->_setAlign(old, mIsMargin || margin);

		// запоминаем текущее состояние
		mIsMargin = margin;

	}

	void Widget::setCoord(const IntCoord & _coord)
	{
		// обновляем абсолютные координаты
		mAbsolutePosition += _coord.point() - mCoord.point();

		for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) (*widget)->_updateAbsolutePoint();
		for (VectorWidgetPtr::iterator widget = mWidgetChildSkin.begin(); widget != mWidgetChildSkin.end(); ++widget) (*widget)->_updateAbsolutePoint();

		// устанавливаем новую координату а старую пускаем в расчеты
		IntCoord old = mCoord;
		mCoord = _coord;

		bool show = true;

		// обновляем выравнивание
		bool margin = mCroppedParent ? _checkMargin() : false;

		if (margin) {
			// проверка на полный выход за границу
			if (_checkOutside()) {
				// скрываем
				show = false;
			}
		}

		_setVisible(show);

		// передаем старую координату , до вызова, текущая координата отца должна быть новой
		for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) (*widget)->_setAlign(old, mIsMargin || margin);
		for (VectorWidgetPtr::iterator widget = mWidgetChildSkin.begin(); widget != mWidgetChildSkin.end(); ++widget) (*widget)->_setAlign(old, mIsMargin || margin);
		for (VectorSubWidget::iterator skin = mSubSkinChild.begin(); skin != mSubSkinChild.end(); ++skin) (*skin)->_setAlign(old, mIsMargin || margin);

		// запоминаем текущее состояние
		mIsMargin = margin;

	}

	void Widget::_attachToLayerItemKeeper(LayerItemKeeper * _item, bool _deep)
	{
		MYGUI_DEBUG_ASSERT(null != _item, "attached item must be valid");

		// сохраняем, чтобы последующие дети могли приаттачиться
		setLayerItemKeeper(_item);

		RenderItem * renderItem = null;

		for (VectorSubWidget::iterator skin = mSubSkinChild.begin(); skin != mSubSkinChild.end(); ++skin) {
			// создаем только если есть хоть один не текстовой сабскин
			if ((null == renderItem) && (*skin)->firstQueue()) {
				renderItem = _item->addToRenderItem(mTexture, true, false);
			}
			(*skin)->_createDrawItem(_item, renderItem);
		}

		for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) {
			// разые типа атачим по разному
			WidgetType type = (*widget)->mWidgetType;

			// всплывающие не трогаем
			if (type == WidgetType::Popup) {
			}
			// чилды как обычно
			else if (type == WidgetType::Child) {
				(*widget)->_attachToLayerItemKeeper(_item, _deep);
			}
			// там свои айтемы
			else if (type == WidgetType::Overlapped) {
				// создаем оверлаппеду новый айтем
				if (_deep) (*widget)->_attachToLayerItemKeeper(_item->createItem(), _deep);
			}

		}
		for (VectorWidgetPtr::iterator widget = mWidgetChildSkin.begin(); widget != mWidgetChildSkin.end(); ++widget)
		{
			(*widget)->_attachToLayerItemKeeper(_item, _deep);
		}
	}

	void Widget::_detachFromLayerItemKeeper(bool _deep)
	{

		for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) {
			// разые типа атачим по разному
			WidgetType type = (*widget)->mWidgetType;

			// всплывающие не трогаем
			if (type == WidgetType::Popup) {
			}
			// чилды как обычно
			else if (type == WidgetType::Child) {
				(*widget)->_detachFromLayerItemKeeper(_deep);
			}
			// там свои леер айтемы
			else if (type == WidgetType::Overlapped) {
				// глубокая очистка
				if (_deep) (*widget)->_detachFromLayerItemKeeper(_deep);
			}

		}

		// мы уже отаттачены
		LayerItemKeeper * layer_item = getLayerItemKeeper();
		if (layer_item) {

			for (VectorWidgetPtr::iterator widget = mWidgetChildSkin.begin(); widget != mWidgetChildSkin.end(); ++widget) (*widget)->_detachFromLayerItemKeeper(_deep);
			for (VectorSubWidget::iterator skin = mSubSkinChild.begin(); skin != mSubSkinChild.end(); ++skin) (*skin)->_destroyDrawItem();

			// при глубокой очистке, если мы оверлаппед, то для нас создавали айтем
			if (_deep && mWidgetType == WidgetType::Overlapped) {
				//layer_item->destroyItem();
			}
			// очищаем
			setLayerItemKeeper(null);
		}
	}

	void Widget::_setUVSet(const FloatRect& _rect)
	{
		if (null != mMainSkin) mMainSkin->_setUVSet(_rect);
	}

	void Widget::_setTextureName(const Ogre::String& _texture)
	{
		if (_texture == mTexture) return;
		mTexture = _texture;

		// если мы приаттаченны, то детачим себя, меняем текстуру, и снова аттачим
		LayerItemKeeper * layer_item = getLayerItemKeeper();
		if (layer_item) {
			// позже сделать детач без текста
			_detachFromLayerItemKeeper();
			mTexture = _texture;
			_attachToLayerItemKeeper(layer_item);
		}

	}

	const Ogre::String& Widget::_getTextureName()
	{
		return mTexture;
	}

	void Widget::_setVisible(bool _visible)
	{
		if (mVisible == _visible) return;
		mVisible = _visible;

		// просто обновляем
		for (VectorSubWidget::iterator skin = mSubSkinChild.begin(); skin != mSubSkinChild.end(); ++skin) {
			(*skin)->_updateView();
		}
	}

	void Widget::show()
	{
		if (mShow) return;
		mShow = true;

		if (mInheritedShow) {
			for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) (*widget)->_inheritedShow();
			for (VectorWidgetPtr::iterator widget = mWidgetChildSkin.begin(); widget != mWidgetChildSkin.end(); ++widget) (*widget)->_inheritedShow();
			for (VectorSubWidget::iterator skin = mSubSkinChild.begin(); skin != mSubSkinChild.end(); ++skin) (*skin)->show();
		}

	}

	void Widget::hide()
	{
		if (false == mShow) return;
		mShow = false;

		// если мы уже скрыты отцом, то рассылать не нужно
		if (mInheritedShow) {
			for (VectorSubWidget::iterator skin = mSubSkinChild.begin(); skin != mSubSkinChild.end(); ++skin) (*skin)->hide();
			for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) (*widget)->_inheritedHide();
			for (VectorWidgetPtr::iterator widget = mWidgetChildSkin.begin(); widget != mWidgetChildSkin.end(); ++widget) (*widget)->_inheritedHide();
		}

	}

	void Widget::_inheritedShow()
	{
		if (mInheritedShow) return;
		mInheritedShow = true;

		if (mShow) {
			for (VectorSubWidget::iterator skin = mSubSkinChild.begin(); skin != mSubSkinChild.end(); ++skin) (*skin)->show();
			for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) (*widget)->_inheritedShow();
			for (VectorWidgetPtr::iterator widget = mWidgetChildSkin.begin(); widget != mWidgetChildSkin.end(); ++widget) (*widget)->_inheritedShow();
		}

	}

	void Widget::_inheritedHide()
	{
		if (false == mInheritedShow) return;
		mInheritedShow = false;

		if (mShow) {
			for (VectorSubWidget::iterator skin = mSubSkinChild.begin(); skin != mSubSkinChild.end(); ++skin) (*skin)->hide();
			for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) (*widget)->_inheritedHide();
			for (VectorWidgetPtr::iterator widget = mWidgetChildSkin.begin(); widget != mWidgetChildSkin.end(); ++widget) (*widget)->_inheritedHide();
		}

	}

	// дает приоритет виджету при пиккинге
	void Widget::_forcePeek(WidgetPtr _widget)
	{
		size_t size = mWidgetChild.size();
		if ( (size < 2) || (mWidgetChild[size-1] == _widget) ) return;
		for (size_t pos=0; pos<size; pos++) {
			if (mWidgetChild[pos] == _widget) {
				mWidgetChild[pos] = mWidgetChild[size-1];
				mWidgetChild[size-1] = _widget;
				return;
			}
		}
	}

	const std::string& Widget::getLayerName()
	{
		LayerKeeper * keeper = getLayerKeeper();
		if (null == keeper) {
			static std::string empty;
			return empty;
		}
		return keeper->getName();
	}

	void Widget::getContainer(WidgetPtr & _list, size_t & _index)
	{
		_list = null;
		_index = ITEM_NONE;
		requestGetContainer(this, _list, _index);
	}

	WidgetPtr Widget::findWidget(const std::string & _name)
	{
		if (_name == mName) return this;
		if (mWidgetClient) return mWidgetClient->findWidget(_name);
		for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) {
			WidgetPtr find = (*widget)->findWidget(_name);
			if (null != find) return find;
		}
		return null;
	}

	void Widget::setNeedToolTip(bool _need)
	{
		if (mNeedToolTip == _need) return;
		mNeedToolTip = _need;

		if (mNeedToolTip) {
			Gui::getInstance().eventFrameStart += newDelegate(this, &Widget::frameEntered);
			mToolTipCurrentTime = 0;
		}
		else {
			Gui::getInstance().eventFrameStart -= newDelegate(this, &Widget::frameEntered);
		}
	}

	void Widget::frameEntered(float _frame)
	{
		if ( ! mEnableToolTip) return;

		IntPoint point = InputManager::getInstance().getMousePosition();

		if (mToolTipOldPoint != point) {

			mToolTipCurrentTime = 0;

			bool inside = getAbsoluteRect().inside(point);
			if (inside) {
				inside = false;
				// проверяем не перекрывают ли нас
				WidgetPtr widget = InputManager::getInstance().getMouseFocusWidget();
				while (widget != 0) {
					if (this->compare(widget)) {
						inside = true;
						break;
					}
					widget = widget->getParent();
				}

				if (inside) {
					// теперь смотрим, не поменялся ли индекс внутри окна
					size_t index = getContainerIndex(point);
					if (mToolTipOldIndex != index) {
						if (mToolTipVisible) {
							mToolTipCurrentTime = 0;
							mToolTipVisible = false;
							eventToolTip(this, ToolTipInfo(ToolTipInfo::Hide));
						}
						mToolTipOldIndex = index;
					}

				}
				else {
					if (mToolTipVisible) {
						mToolTipCurrentTime = 0;
						mToolTipVisible = false;
						eventToolTip(this, ToolTipInfo(ToolTipInfo::Hide));
					}
				}

			}
			else {
				if (mToolTipVisible) {
					mToolTipCurrentTime = 0;
					mToolTipVisible = false;
					eventToolTip(this, ToolTipInfo(ToolTipInfo::Hide));
				}
			}

			mToolTipOldPoint = point;
		}
		else {

			bool inside = getAbsoluteRect().inside(point);
			if (inside) {
				inside = false;
				// проверяем не перекрывают ли нас
				WidgetPtr widget = InputManager::getInstance().getMouseFocusWidget();
				while (widget != 0) {
					if (widget->getName() == mName) {
						inside = true;
						break;
					}
					widget = widget->getParent();
				}

				if (inside) {
					if ( ! mToolTipVisible) {
						mToolTipCurrentTime += _frame;
						if (mToolTipCurrentTime > WIDGET_TOOLTIP_TIMEOUT) {
							mToolTipVisible = true;
							eventToolTip(this, ToolTipInfo(ToolTipInfo::Show, mToolTipOldIndex, point));
						}
					}
				}
			}
		}
	}

	void Widget::enableToolTip(bool _enable)
	{
		if (_enable == mEnableToolTip) return;
		mEnableToolTip = _enable;

		if ( ! mEnableToolTip) {
			if (mToolTipVisible) {
				mToolTipCurrentTime = 0;
				mToolTipVisible = false;
				eventToolTip(this, ToolTipInfo(ToolTipInfo::Hide));
			}
		}
		else {
			mToolTipCurrentTime = 0;
		}
	}

	void Widget::resetContainer(bool _updateOnly)
	{
		if ( mEnableToolTip) {
			if (mToolTipVisible) {
				mToolTipVisible = false;
				eventToolTip(this, ToolTipInfo(ToolTipInfo::Hide));
			}
			mToolTipCurrentTime = 0;
			mToolTipOldIndex = ITEM_NONE;
		}
	}

	void Widget::setMaskPeek(const std::string & _filename)
	{
		if (mOwnMaskPeekInfo.load(_filename)) {
			mMaskPeekInfo = &mOwnMaskPeekInfo;
		}
		else {
			MYGUI_LOG(Error, "mask not load '" << _filename << "'");
		}
	}

	void Widget::setRealPosition(const FloatPoint & _point)
	{
		const IntCoord & coord = WidgetManager::getInstance().convertRelativeToInt(FloatCoord(_point.left, _point.top, 0, 0), this);
		setPosition(coord.point());
	}

	void Widget::setRealSize(const FloatSize & _size)
	{
		const IntCoord & coord = WidgetManager::getInstance().convertRelativeToInt(FloatCoord(0, 0, _size.width, _size.height), this);
		setSize(coord.size());
	}

	void Widget::setRealCoord(const FloatCoord & _coord)
	{
		const IntCoord & coord = WidgetManager::getInstance().convertRelativeToInt(_coord, this);
		setCoord(coord);
	}

	void Widget::_linkChildWidget(WidgetPtr _widget)
	{
		VectorWidgetPtr::iterator iter = std::find(mWidgetChild.begin(), mWidgetChild.end(), _widget);
		MYGUI_ASSERT(iter == mWidgetChild.end(), "widget already exist");
		mWidgetChild.push_back(_widget);
	}

	void Widget::_unlinkChildWidget(WidgetPtr _widget)
	{
		VectorWidgetPtr::iterator iter = std::remove(mWidgetChild.begin(), mWidgetChild.end(), _widget);
		MYGUI_ASSERT(iter != mWidgetChild.end(), "widget not found");
		mWidgetChild.erase(iter);
	}

	/*void Widget::detachFromLayer()
	{

		// если мы рут, то просто отсоединяем от леера
		if (isRootWidget()) {
			LayerManager::getInstance().detachFromLayerKeeper(this);
		}
		// если мы не рут, то отсоединяем от леер кипера
		else {
			_detachFromLayerItemKeeper();
			mCroppedParent = null;
		}

	}
	
	void Widget::attachToLayer(const std::string& _layername)
	{
		detachFromLayer();
		// присоединяем как дочку
		if (_layername.empty()) {
			// если есть отец, то присоединяем к его рендрингу
			// если нет, то виджет так и будет болтаться
			WidgetPtr parent = getParent();
			if (parent) {
				mAbsolutePosition = parent->getAbsolutePosition() + mCoord.point();
				mCroppedParent = parent;

				// если есть леер, то атачимся
				LayerItemKeeper * layer_item = parent->getLayerItemKeeper();
				if (layer_item) _attachToLayerItemKeeper(layer_item);

				for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) (*widget)->_updateAbsolutePoint();
				for (VectorWidgetPtr::iterator widget = mWidgetChildSkin.begin(); widget != mWidgetChildSkin.end(); ++widget) (*widget)->_updateAbsolutePoint();

				// обновляем свой вью
				_updateView();
			}
		}
		// присоединяем к лееру
		else {
			LayerManager::getInstance().attachToLayerKeeper(_layername, this);

			// обновляем координаты
			mAbsolutePosition = mCoord.point();
			// сбрасываем обрезку
			mMargin.clear();

			for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) (*widget)->_updateAbsolutePoint();
			for (VectorWidgetPtr::iterator widget = mWidgetChildSkin.begin(); widget != mWidgetChildSkin.end(); ++widget) (*widget)->_updateAbsolutePoint();

			// обновляем свой вью
			_updateView();
		}
	}

	void Widget::detachFromWidget()
	{
		// проверить альфу и видимость

		// мы рут достаточно поменять отца на самого рутового
		if (isRootWidget()) {

			if (getParent()) {

				// нам нужен самый рутовый парент
				WidgetPtr root = getParent();
				while (root->getParent()) { root = root->getParent(); }

				mIWidgetCreator = root->mIWidgetCreator;
				mIWidgetCreator->_linkChildWidget(this);
				mParent->_unlinkChildWidget(this);
				mParent = null;

			}
		}
		else {

			// отдетачиваемся от лееров
			detachFromLayer();

			// нам нужен самый рутовый парент
			WidgetPtr root = getParent();
			while (root->getParent()) { root = root->getParent(); }

			// присоединяем себя к самой верхней иерархии
			mIWidgetCreator = root->mIWidgetCreator;
			mIWidgetCreator->_linkChildWidget(this);
			mParent->_unlinkChildWidget(this);
			mParent = null;

			// если у самого высокого есть леер, то туда же
			LayerKeeper * layer = root->getLayerKeeper();
			if (layer) attachToLayer(layer->getName());

		}

	}

	void Widget::attachToWidget(WidgetPtr _widget)
	{
		MYGUI_ASSERT(_widget, "parent mast be valid");
		MYGUI_ASSERT(_widget != this, "cicle attach");

		// запоминаем уровень
		LayerKeeper * layer = getLayerKeeper();

		// проверяем на цикличность атача
		WidgetPtr parent = _widget;
		while (parent->getParent()) {
			MYGUI_ASSERT(parent != this, "cicle attach");
			parent = parent->getParent();
		}

		// отдетачиваемся от лееров
		detachFromLayer();

		mIWidgetCreator->_unlinkChildWidget(this);
		mIWidgetCreator = _widget;
		mParent = _widget;
		mParent->_linkChildWidget(this);

		// присоединяем как дочку
		attachToLayer(layer ? layer->getName() : "");

	}*/

	void Widget::setWidgetType(WidgetType _type)
	{
		if (_type == mWidgetType) return;
		
		// ищем леер к которому мы присоедененны
		WidgetPtr root = this;
		while (!root->isRootWidget())
		{
			root = root->getParent();
		};

		// отсоединяем рут
		std::string layername;
		LayerKeeper * layer = root->getLayerKeeper();
		if (layer)
		{
			layername = layer->getName();
			LayerManager::getInstance().detachFromLayerKeeper(root);

			// если мы рут, то придется отцеплят более высокого рута
			if (root == this)
			{
				layername.clear();

				if (getParent())
				{
					// ищем леер к которому мы присоедененны
					root = getParent();
					while (!root->isRootWidget())
					{
						root = root->getParent();
					};

					layer = root->getLayerKeeper();
					if (layer)
					{
						layername = layer->getName();
						LayerManager::getInstance().detachFromLayerKeeper(root);
					}

				}
			}
		}

		// корректируем
		mWidgetType = _type;
		if (_type == WidgetType::Child) {

			WidgetPtr parent = getParent();
			if (parent) {
				mAbsolutePosition = parent->getAbsolutePosition() + mCoord.point();
				mCroppedParent = parent;
				for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) (*widget)->_updateAbsolutePoint();
				for (VectorWidgetPtr::iterator widget = mWidgetChildSkin.begin(); widget != mWidgetChildSkin.end(); ++widget) (*widget)->_updateAbsolutePoint();
			}

		}
		else if (_type == WidgetType::Popup) {

			mCroppedParent = null;
			// обновляем координаты
			mAbsolutePosition = mCoord.point();
			// сбрасываем обрезку
			mMargin.clear();

			for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) (*widget)->_updateAbsolutePoint();
			for (VectorWidgetPtr::iterator widget = mWidgetChildSkin.begin(); widget != mWidgetChildSkin.end(); ++widget) (*widget)->_updateAbsolutePoint();

		}
		else if (_type == WidgetType::Overlapped) {

			WidgetPtr parent = getParent();
			if (parent) {
				mAbsolutePosition = parent->getAbsolutePosition() + mCoord.point();
				mCroppedParent = parent;
				for (VectorWidgetPtr::iterator widget = mWidgetChild.begin(); widget != mWidgetChild.end(); ++widget) (*widget)->_updateAbsolutePoint();
				for (VectorWidgetPtr::iterator widget = mWidgetChildSkin.begin(); widget != mWidgetChildSkin.end(); ++widget) (*widget)->_updateAbsolutePoint();
			}

		}

		// присоединяем обратно
		if (!layername.empty()) {
			LayerManager::getInstance().attachToLayerKeeper(layername, root);
		}

	}

	WidgetPtr Widget::getLogicalParent()
	{
		WidgetPtr result = mParent;
		WidgetPtr parent = mParent;

		if (parent) {
			if (WidgetPtr parent2 = parent->getParent()) {
				if (parent2) {
					if (parent2->getClientWidget() == parent) {
						result = parent2;
					}
				}
			}
		}

		return result;
	}

} // namespace MyGUI

