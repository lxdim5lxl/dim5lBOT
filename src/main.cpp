#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <vector>
#include <fstream>

using namespace geode::prelude;

// ===== Macro Data =====
struct MacroFrame {
    int frame;
    bool player1Hold;
    bool player2Hold;
};

std::vector<MacroFrame> macroFrames;
bool isRecording = false;
bool isPlaying = false;
int currentFrame = 0;
int playbackIndex = 0;

// ===== PlayLayer Hook =====
class $modify(PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        currentFrame = 0;
        playbackIndex = 0;
    }

    void update(float dt) {
        PlayLayer::update(dt);

        if (isRecording) {
            MacroFrame f;
            f.frame = currentFrame;
            f.player1Hold = m_player1->m_isHolding;
            f.player2Hold = m_player2 ? m_player2->m_isHolding : false;
            macroFrames.push_back(f);
            currentFrame++;
        }

        if (isPlaying && playbackIndex < macroFrames.size()) {
            auto& f = macroFrames[playbackIndex];
            if (f.frame == currentFrame) {
                if (f.player1Hold) {
                    m_player1->pushButton(PlayerButton::Jump);
                } else {
                    m_player1->releaseButton(PlayerButton::Jump);
                }
                playbackIndex++;
            }
            currentFrame++;
        }
    }
};

// ===== Bot Menu Layer =====
class BotMenuLayer : public CCLayer {
public:
    static BotMenuLayer* create() {
        auto ret = new BotMenuLayer();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init() {
        if (!CCLayer::init()) return false;

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        // Background
        auto bg = CCScale9Sprite::create("GJ_square01.png");
        bg->setContentSize({320, 260});
        bg->setPosition(winSize / 2);
        addChild(bg);

        // Title
        auto title = CCLabelBMFont::create("dim5lBOT", "goldFont.fnt");
        title->setPosition(winSize.width / 2, winSize.height / 2 + 100);
        title->setScale(0.8f);
        addChild(title);

        // Record button
        auto recBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Record", "goldFont.fnt", "GJ_button_01.png"),
            this, menu_selector(BotMenuLayer::onRecord)
        );

        // Play button
        auto playBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Play", "goldFont.fnt", "GJ_button_02.png"),
            this, menu_selector(BotMenuLayer::onPlay)
        );

        // Stop button
        auto stopBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Stop", "goldFont.fnt", "GJ_button_06.png"),
            this, menu_selector(BotMenuLayer::onStop)
        );

        // Close button
        auto closeBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Close", "goldFont.fnt", "GJ_button_05.png"),
            this, menu_selector(BotMenuLayer::onClose)
        );

        auto menu = CCMenu::create(recBtn, playBtn, stopBtn, closeBtn, nullptr);
        menu->alignItemsVerticallyWithPadding(8);
        menu->setPosition(winSize / 2);
        addChild(menu);

        return true;
    }

    void onRecord(CCObject*) {
        macroFrames.clear();
        isRecording = true;
        isPlaying = false;
        FLAlertLayer::create("dim5lBOT", "Recording started!", "OK")->show();
    }

    void onPlay(CCObject*) {
        if (macroFrames.empty()) {
            FLAlertLayer::create("dim5lBOT", "No macro recorded!", "OK")->show();
            return;
        }
        isRecording = false;
        isPlaying = true;
        playbackIndex = 0;
        currentFrame = 0;
        FLAlertLayer::create("dim5lBOT", "Playback started!", "OK")->show();
    }

    void onStop(CCObject*) {
        isRecording = false;
        isPlaying = false;
        FLAlertLayer::create("dim5lBOT", "Stopped!", "OK")->show();
    }

    void onClose(CCObject*) {
        removeFromParentAndCleanup(true);
    }
};

// ===== Add Button to Main Menu =====
class $modify(MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto botBtn = CCMenuItemSpriteExtra::create(
            CircleButtonSprite::createWithSpriteFrameName("geode.loader/geode-logo-outline-gold.png"),
            this,
            menu_selector(ModifiedMenuLayer::onBotMenu)
        );

        auto menu = CCMenu::create(botBtn, nullptr);
        menu->setPosition(CCPoint(30, 30));
        addChild(menu);

        return true;
    }

    void onBotMenu(CCObject*) {
        auto layer = BotMenuLayer::create();
        addChild(layer);
    }
};
