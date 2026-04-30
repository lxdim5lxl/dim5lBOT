#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <vector>
#include <fstream>
#include <filesystem>
#include <sstream>

using namespace geode::prelude;

// ===== Macro Data =====
struct MacroInput {
    int pressFrame;
    int releaseFrame;
    bool player2;
};

std::vector<MacroInput> macroInputs;
bool isRecording = false;
bool isPlaying = false;
int currentFrame = 0;
int playbackIndex = 0;

// Temp recording state
bool prevP1 = false;
bool prevP2 = false;
int pressStartP1 = -1;
int pressStartP2 = -1;

std::filesystem::path getSavePath() {
    auto path = std::filesystem::path(geode::dirs::getSaveDir()) / "dim5lBOT";
    std::filesystem::create_directories(path);
    return path / "macro.txt";
}

void saveMacro() {
    std::ofstream file(getSavePath());
    for (auto& input : macroInputs) {
        file << input.pressFrame << "," << input.releaseFrame;
        if (input.player2) file << ",p2";
        file << "\n";
    }
    file.close();
}

void loadMacro() {
    std::ifstream file(getSavePath());
    if (!file.is_open()) return;
    macroInputs.clear();
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        std::vector<std::string> parts;
        while (std::getline(ss, token, ',')) {
            parts.push_back(token);
        }
        if (parts.size() < 2) continue;
        MacroInput input;
        input.pressFrame = std::stoi(parts[0]);
        input.releaseFrame = std::stoi(parts[1]);
        input.player2 = (parts.size() >= 3 && parts[2] == "p2");
        macroInputs.push_back(input);
    }
    file.close();
}

// ===== PlayLayer Hook =====
class $modify(PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        currentFrame = 0;
        playbackIndex = 0;
        prevP1 = false;
        prevP2 = false;
        pressStartP1 = -1;
        pressStartP2 = -1;
    }

    void update(float dt) {
        PlayLayer::update(dt);

        if (isRecording) {
            bool p1 = m_player1->m_jumpBuffered;
            bool p2 = m_player2 ? m_player2->m_jumpBuffered : false;

            // Player 1
            if (p1 && !prevP1) {
                pressStartP1 = currentFrame;
            } else if (!p1 && prevP1 && pressStartP1 >= 0) {
                macroInputs.push_back({pressStartP1, currentFrame, false});
                pressStartP1 = -1;
            }
            prevP1 = p1;

            // Player 2
            if (p2 && !prevP2) {
                pressStartP2 = currentFrame;
            } else if (!p2 && prevP2 && pressStartP2 >= 0) {
                macroInputs.push_back({pressStartP2, currentFrame, true});
                pressStartP2 = -1;
            }
            prevP2 = p2;

            currentFrame++;
        }

        if (isPlaying && playbackIndex < (int)macroInputs.size()) {
            auto& input = macroInputs[playbackIndex];

            if (currentFrame == input.pressFrame) {
                auto player = input.player2 ? m_player2 : m_player1;
                if (player) player->pushButton(PlayerButton::Jump);
            }
            if (currentFrame == input.releaseFrame) {
                auto player = input.player2 ? m_player2 : m_player1;
                if (player) player->releaseButton(PlayerButton::Jump);
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

        auto bg = CCScale9Sprite::create("GJ_square01.png");
        bg->setContentSize({320, 300});
        bg->setPosition(winSize / 2);
        addChild(bg);

        auto title = CCLabelBMFont::create("dim5lBOT", "goldFont.fnt");
        title->setPosition(winSize.width / 2, winSize.height / 2 + 120);
        title->setScale(0.8f);
        addChild(title);

        auto recBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Record", "goldFont.fnt", "GJ_button_01.png"),
            this, menu_selector(BotMenuLayer::onRecord)
        );
        auto playBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Play", "goldFont.fnt", "GJ_button_02.png"),
            this, menu_selector(BotMenuLayer::onPlay)
        );
        auto stopBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Stop", "goldFont.fnt", "GJ_button_06.png"),
            this, menu_selector(BotMenuLayer::onStop)
        );
        auto saveBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Save", "goldFont.fnt", "GJ_button_03.png"),
            this, menu_selector(BotMenuLayer::onSave)
        );
        auto loadBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Load", "goldFont.fnt", "GJ_button_04.png"),
            this, menu_selector(BotMenuLayer::onLoad)
        );
        auto closeBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Close", "goldFont.fnt", "GJ_button_05.png"),
            this, menu_selector(BotMenuLayer::onClose)
        );

        auto menu = CCMenu::create(recBtn, playBtn, stopBtn, saveBtn, loadBtn, closeBtn, nullptr);
        menu->alignItemsVerticallyWithPadding(6);
        menu->setPosition(winSize / 2);
        addChild(menu);

        return true;
    }

    void onRecord(CCObject*) {
        macroInputs.clear();
        isRecording = true;
        isPlaying = false;
        FLAlertLayer::create("dim5lBOT", "Recording started!", "OK")->show();
    }

    void onPlay(CCObject*) {
        if (macroInputs.empty()) {
            FLAlertLayer::create("dim5lBOT", "No macro loaded!", "OK")->show();
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

    void onSave(CCObject*) {
        if (macroInputs.empty()) {
            FLAlertLayer::create("dim5lBOT", "Nothing to save!", "OK")->show();
            return;
        }
        saveMacro();
        FLAlertLayer::create("dim5lBOT", "Macro saved!", "OK")->show();
    }

    void onLoad(CCObject*) {
        loadMacro();
        if (macroInputs.empty()) {
            FLAlertLayer::create("dim5lBOT", "No save file found!", "OK")->show();
            return;
        }
        FLAlertLayer::create("dim5lBOT", "Macro loaded!", "OK")->show();
    }

    void onClose(CCObject*) {
        removeFromParentAndCleanup(true);
    }
};

// ===== Add Button to Main Menu =====
class $modify(MyMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto botBtn = CCMenuItemSpriteExtra::create(
            CircleButtonSprite::createWithSpriteFrameName("geode.loader/geode-logo-outline-gold.png"),
            this,
            menu_selector(MyMenuLayer::onBotMenu)
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
