#include "rowl/state/game_state.hpp"
#include "rowl/state/session_persistence.hpp"
#include "rowl/core/logger.hpp"
#include <iostream>
#include <vector>
int main(){
 Rowl::Core::Logger::setLogLevel(Rowl::Core::LogLevel::Critical);
 auto s=Rowl::State::GameState::createInitialState(101);
 std::vector<Rowl::State::DialogueHistoryEntry> e;
 for(int i=0;i<500;++i){Rowl::State::DialogueHistoryEntry h;h.nodeId=101;h.dialogue=std::string(2000,'x');e.push_back(h);}
 s=Rowl::State::GameState::withDialogueHistory(s,e);
 for(int i=0;i<4;++i)s=Rowl::State::GameState::createNextState(s,101);
 std::string j=s->serializeJson();
 Rowl::State::SessionPersistence p("/tmp/rowl-audit-saves");
 std::cout<<"valid dialogue entries="<<s->dialogueHistory->size()<<" chars=2000 json_bytes="<<j.size()<<" save="<<p.saveSlot(s,0)<<" decode="<<bool(Rowl::State::GameState::decodeJson(j).state)<<std::endl;
}
