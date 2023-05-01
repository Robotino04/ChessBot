#include "CLI/playMode.hpp"
#include "CLI/IO.hpp"

#include "CLI/popen2.hpp"

#include "Thera/Board.hpp"
#include "Thera/Move.hpp"
#include "Thera/MoveGenerator.hpp"

#include "Thera/Utils/ChessTerms.hpp"

#include "Thera/perft.hpp"

#include "ANSI/ANSI.hpp"

#include <array>
#include <map>
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <functional>
#include <fstream>
#include <bitset>
#include <unordered_set>

static const float highlightOpacity = 0.5;
static const RGB highlightMovePossible = {82, 255, 220};
static const RGB highlightSquareSelected = {247, 92, 255};
static const RGB highlightBitboardPresent = {255, 242, 0};

static void printBoard(Thera::Board const& board, std::array<RGB, 64> const& squareHighlights, Options const& options){

	const RGB whiteBoardColor = {255, 210, 153};
	const RGB blackBoardColor = {130, 77, 39};
	const RGB whitePieceColorOnWhiteSquare = {80, 80, 80};
	const RGB whitePieceColorOnBlackSquare = {180, 180, 180};
	const RGB blackPieceColor = {0, 0, 0};

	using PC = Thera::PieceColor;
	using PT = Thera::PieceType;
	static const std::map<std::pair<PC, PT>, std::string> pieces = {
		{{PC::White, PT::None}, " "},
		{{PC::Black, PT::None}, " "},
		{{PC::White, PT::Pawn}, "♙"},
		{{PC::Black, PT::Pawn}, "♟"},
		{{PC::White, PT::Bishop}, "♗"},
		{{PC::Black, PT::Bishop}, "♝"},
		{{PC::White, PT::Knight}, "♘"},
		{{PC::Black, PT::Knight}, "♞"},
		{{PC::White, PT::Rook}, "♖"},
		{{PC::Black, PT::Rook}, "♜"},
		{{PC::White, PT::Queen}, "♕"},
		{{PC::Black, PT::Queen}, "♛"},
		{{PC::White, PT::King}, "♔"},
		{{PC::Black, PT::King}, "♚"},
	};

	std::cout << ANSI::set4BitColor(ANSI::Gray, ANSI::Background) << "  a b c d e f g h   " << ANSI::reset()  << "\n";	
	for(int y=7; y >= 0; y--){
		std::cout << ANSI::set4BitColor(ANSI::Gray, ANSI::Background) << y+1 << " ";
		for(int x=0; x<8; x++){
			RGB boardColor = (x + y)%2 ? whiteBoardColor : blackBoardColor;

			const Thera::Coordinate square(x, y);
			
			if (squareHighlights.at(square.getIndex64()) != RGB::Black)
				boardColor = overlay(boardColor, squareHighlights.at(square.getIndex64()), highlightOpacity);

			// set the board color
			std::cout << ANSI::set24BitColor(boardColor.red, boardColor.green, boardColor.blue, ANSI::Background);

			// set the piece color
			const RGB pieceColor = board.at(square).getColor() == Thera::PieceColor::White
				? ((x + y)%2 ? whitePieceColorOnWhiteSquare : whitePieceColorOnBlackSquare)
				: blackPieceColor;
			if (board.at(square).getType() != Thera::PieceType::None){
				std::cout << ANSI::set24BitColor(pieceColor.red, pieceColor.green, pieceColor.blue, ANSI::Foreground);
			}

			// print the piece
			std::cout
				<< pieces.at({board.at(square).getColor(), board.at(square).getType()})
				<< " ";
		}
		std::cout
			<< ANSI::set4BitColor(ANSI::Gray, ANSI::Background) << ANSI::reset(ANSI::Foreground) << y+1 << " " << ANSI::reset();

		// print board stats
		std::cout << "  ";
		switch(7-y){
			case 0:
				std::cout << (board.getColorToMove() == Thera::PieceColor::White ? "White" : "Black")
						  << " to move.";
				break;
			case 1:
				std::cout << "Castling: [White] [Black]";
				break;
			case 2:
				std::cout << "          ";
				std::cout << setConditionalColor(board.getCurrentState().canWhiteCastleLeft, ANSI::Background) << "[Q]";
				std::cout << ANSI::reset(ANSI::Background) << " ";
				std::cout << setConditionalColor(board.getCurrentState().canWhiteCastleRight, ANSI::Background) << "[K]";
				std::cout << ANSI::reset(ANSI::Background) << " ";
				std::cout << setConditionalColor(board.getCurrentState().canBlackCastleLeft, ANSI::Background) << "[Q]";
				std::cout << ANSI::reset(ANSI::Background) << " ";
				std::cout << setConditionalColor(board.getCurrentState().canBlackCastleRight, ANSI::Background) << "[K]";

				break;
			case 3:
				switch(options.selectedBitboard){
					case BitboardSelection::AllPieces:
						std::cout << "Showing bitboard for all pieces";
						break;
					case BitboardSelection::Debug:
						std::cout << "Showing debug bitboard";
						break;
					case BitboardSelection::PinnedPieces:
						std::cout << "Showing pinned pieces";
						break;
					case BitboardSelection::SinglePiece:
						std::cout << "Showing bitboard for " << Thera::Utils::pieceToString(options.shownPieceBitboard, true);
						break;
					case BitboardSelection::AttackedSquares:
						std::cout << "Showing attacked squares";
						break;
					case BitboardSelection::None:
						std::cout << "Showing no bitboard";
						break;
				}
				break;
			case 5:
				std::cout << "FEN: " << ANSI::set4BitColor(ANSI::Blue, ANSI::Foreground) << board.storeToFEN() << ANSI::reset(ANSI::Foreground);
			default:
				break;
		}

		std::cout << ANSI::reset() << "\n";
	}
	
	std::cout << ANSI::set4BitColor(ANSI::Gray, ANSI::Background) << "  a b c d e f g h   " << ANSI::reset()  << "\n";	
}

struct MoveInputResult{
	enum OperationType{
		MakeMove,
		UndoMove,
		Continue,
		ForceMove,
		Perft,
		Exit,
		LoadFEN,
		Analyze,
		FlipColors,
	};

	Thera::Move move;
	OperationType op = OperationType::MakeMove;
	int perftDepth=0;
	bool failed = false;
	std::string message;
};

static void handleShowCommand(MoveInputResult& result, Options& options){
	static const std::map<std::string, Thera::PieceType> stringToPieceType = {
		{"p", Thera::PieceType::Pawn},
		{"b", Thera::PieceType::Bishop},
		{"n", Thera::PieceType::Knight},
		{"r", Thera::PieceType::Rook},
		{"q", Thera::PieceType::Queen},
		{"k", Thera::PieceType::King},

		{"pawn", 	Thera::PieceType::Pawn},
		{"bishop", 	Thera::PieceType::Bishop},
		{"knight", 	Thera::PieceType::Knight},
		{"rook", 	Thera::PieceType::Rook},
		{"queen", 	Thera::PieceType::Queen},
		{"king", 	Thera::PieceType::King},
	};
	static const std::map<std::string, Thera::PieceColor> stringToPieceColor = {
		{"white", 	Thera::PieceColor::White},
		{"black", 	Thera::PieceColor::Black},
		{"w", 		Thera::PieceColor::White},
		{"b", 		Thera::PieceColor::Black},
	};
	result.op = MoveInputResult::Continue;

	std::string buffer;

	// parse color
	std::cin >> buffer;
	if (buffer == "none"){
		options.selectedBitboard = BitboardSelection::None;
		return;
	}
	else if (buffer == "all"){
		options.selectedBitboard = BitboardSelection::AllPieces;
		return;
	}
	else if (buffer == "debug"){
		options.selectedBitboard = BitboardSelection::Debug;
		return;
	}
	else if (buffer == "pin" || buffer == "pinned"){
		options.selectedBitboard = BitboardSelection::PinnedPieces;
		return;
	}
	else if (buffer == "attacked"){
		options.selectedBitboard = BitboardSelection::AttackedSquares;
		return;
	}

	try{
		options.shownPieceBitboard.setColor(stringToPieceColor.at(buffer));
	}
	catch(std::out_of_range){
		result.message = "Invalid color \"" + buffer + "\"!";
		result.failed = true;
		return;
	}

	// parse type
	std::cin >> buffer;
	try{
		options.shownPieceBitboard.setType(stringToPieceType.at(buffer));
	}
	catch(std::out_of_range){
		result.message = "Invalid piece \"" + buffer + "\"!";
		result.failed = true;
		return;
	}
	options.selectedBitboard = BitboardSelection::SinglePiece;
}

static void handlePerftCommand(MoveInputResult& result, Options& options){
	std::string buffer;

	result.op = MoveInputResult::Perft;

	// parse depth
	try{
		std::cin >> buffer;
		result.perftDepth = std::stoi(buffer);
	}
	catch(std::invalid_argument){
		result.message = "Invalid depth \"" + buffer + "\"!";
		result.op = MoveInputResult::Continue;
		result.failed = true;
		return;
	}
}

static void handleFenCommand(MoveInputResult& result, Options& options){
	result.op = MoveInputResult::LoadFEN;

	std::string buffer;
	std::getline(std::cin, buffer);
	buffer = buffer.substr(1); // remove the leading space

	try{
		Thera::Board testBoard;
		testBoard.loadFromFEN(buffer);
		options.fen = buffer;
	}
	catch(std::invalid_argument){
		result.message = "Invalid FEN string: \"" + buffer + "\"";
		result.op = MoveInputResult::Continue;
		result.failed = true;
		return;
	}
	
}

static void getUserMoveStart(MoveInputResult& result, Options& options){
	std::string buffer;
	std::cout << "Move start: " << std::flush;
	std::cin >> buffer;
	if (buffer == "exit"){
		result.op = MoveInputResult::Exit;
		return;
	}
	else if (buffer == "undo"){
		result.op = MoveInputResult::UndoMove;
		return;
	}
	else if (buffer == "show"){
		handleShowCommand(result, options);
		return;
	}
	else if (buffer == "perft"){
		handlePerftCommand(result, options);
		return;
	}
	else if (buffer == "fen"){
		handleFenCommand(result, options);
		return;
	}
	else if (buffer == "analyze"){
		handlePerftCommand(result, options);
		result.op = MoveInputResult::Analyze;
		return;
	}
	else if (buffer == "flip"){
		result.op = MoveInputResult::FlipColors;
		return;
	}
	else{
		try{
			result.move.startIndex = Thera::Utils::squareFromAlgebraicNotation(buffer.substr(0, 2));
			return;
		}
		catch (std::invalid_argument){
			result.message = "Invalid command or move!";
			result.failed = true;
			return;
		}
	}
	result.failed = true;
	result.message = "Invalid command or move!";
}

static void getUserMoveEnd(MoveInputResult& result, Options& options){
	std::string buffer;
	std::cout << "Move end: " << std::flush;
	std::cin >> buffer;
	if (buffer == "exit"){
		result.op = MoveInputResult::Exit;
		return;
	}
	else if (buffer == "change"){
		result.op = MoveInputResult::Continue;
		return;
	}
	else if (buffer == "undo"){
		result.op = MoveInputResult::UndoMove;
		return;
	}
	else{
		if (buffer.at(buffer.size() - 1) == 'F'){
			result.op = MoveInputResult::ForceMove;
		}
		try{
			result.move.endIndex = Thera::Utils::squareFromAlgebraicNotation(buffer.substr(0, 2));
			return;
		}
		catch (std::invalid_argument){
			result.message = "Invalid command or move!";
			result.failed = true;
			return;
		}
	}
	result.failed = true;
	result.message = "Invalid command or move!";
}

static void setBitboardHighlight(Thera::Bitboard const& bitboard, std::array<RGB, 64>& highlights){
	for (int8_t i=0; i<64; i++){
		if (bitboard[i])
			highlights.at(i) = highlightBitboardPresent;
	}
}

static void setBitboardHighlight(Options const& options, Thera::Board const& board, Thera::MoveGenerator& generator, std::array<RGB, 64>& highlights){
	Thera::Bitboard bitboard;

	switch(options.selectedBitboard){
		case BitboardSelection::AllPieces: bitboard = board.getAllPieceBitboard(); break;
		case BitboardSelection::Debug: bitboard = Thera::MoveGenerator::debugBitboard; break;
		case BitboardSelection::None: bitboard = 0; break;
		case BitboardSelection::PinnedPieces: 
			generator.generateAttackData(board);
			bitboard = generator.getPinnedPieces();
			break;
		case BitboardSelection::SinglePiece: bitboard = board.getBitboard(options.shownPieceBitboard); break;
		case BitboardSelection::AttackedSquares:
			generator.generateAttackData(board);
			bitboard = generator.getAttackedSquares();
			break;
	}
	
	while (bitboard.hasPieces()){
		highlights.at(bitboard.getLS1B()) = highlightBitboardPresent;
		bitboard.clearLS1B();
	}
}

static void redrawGUI(Options const& options, Thera::Board const& board, Thera::MoveGenerator& generator, std::array<RGB, 64>& highlights, std::string const& message){
	std::cout << ANSI::clearScreen() << ANSI::reset() << "-------------------\n" << message << ANSI::reset() << "\n";
	setBitboardHighlight(options, board, generator, highlights);
	printBoard(board, highlights, options);
	highlights.fill(RGB());
}

/**
 * @brief Run perft and compare the result with stockfish.
 * 
 * @param userInput 
 * @param options 
 * @param message 
 */
static void analyzePosition(MoveInputResult const& userInput, Thera::Board& board, Thera::MoveGenerator& generator, std::string& message, int originalDepth){
	if (userInput.perftDepth == 0) return;
	if (userInput.perftDepth == originalDepth) message = "";

	std::string indentation;
	for (int i=0; i<originalDepth-userInput.perftDepth; i++)
		indentation += "\t";
	
	auto [in, out] = popen2("stockfish");

	const std::string positionCMD = "position fen " + board.storeToFEN() + "\n";
	const std::string perftCMD = "go perft " + std::to_string(userInput.perftDepth) + "\n";

	fputs(positionCMD.c_str(), in.get());
	fputs(perftCMD.c_str(), in.get());
	fputs("quit\n", in.get());
	fflush(in.get());

	std::vector<std::pair<Thera::Move, int>> stockfishMoves;
	int stockfishNodesSearched = 0;
	
	while (true){
		char c_buffer[1024];
		if (fgets(c_buffer, sizeof(c_buffer), out.get()) == nullptr) break;
		
		const std::string buffer = c_buffer;

		const std::string NODES_SEARCHED_TEXT = "Nodes searched: ";

		int i=0;
		if (Thera::Utils::isInRange(buffer.at(i++), 'a', 'h') &&
			Thera::Utils::isInRange(buffer.at(i++), '1', '8') && 
			Thera::Utils::isInRange(buffer.at(i++), 'a', 'h') &&
			Thera::Utils::isInRange(buffer.at(i++), '1', '8')
		){
			auto& movePair = stockfishMoves.emplace_back();
			auto& move = movePair.first;
			auto& numSubmoves = movePair.second;

			move.startIndex = Thera::Utils::squareFromAlgebraicNotation(buffer.substr(0, 2));
			move.endIndex = Thera::Utils::squareFromAlgebraicNotation(buffer.substr(2, 2));

			char promotion = buffer.at(i);
			switch(tolower(promotion)){
				case 'b': move.promotionType = Thera::PieceType::Bishop; i++; break;
				case 'n': move.promotionType = Thera::PieceType::Knight; i++; break;
				case 'r': move.promotionType = Thera::PieceType::Rook; i++; break;
				case 'q': move.promotionType = Thera::PieceType::Queen; i++; break;
			}

			if (buffer.at(i++) != ':') continue;
			if (buffer.at(i++) != ' ') continue;
			numSubmoves = stoi(buffer.substr(i));
		}
		else if (buffer.starts_with(NODES_SEARCHED_TEXT)){
			stockfishNodesSearched = stoi(buffer.substr(NODES_SEARCHED_TEXT.size()));
		}
	}

	std::vector<std::pair<Thera::Move, int>> theraMoves;
	std::vector<Thera::Move> theraMovesRaw;
	const auto storeMove = [&](Thera::Move const& move, int numSubmoves){
		theraMoves.emplace_back(move, numSubmoves);
		theraMovesRaw.push_back(move);
	};

	int filteredMoves = 0;
	int theraNodesSearched = Thera::perft(board, generator, userInput.perftDepth, true, storeMove, filteredMoves);
	
	enum class MoveSource{
		Thera,
		Stockfish
	};

	std::vector<std::tuple<Thera::Move, int, MoveSource>> differentMoves;

	for (auto movePair : theraMoves){
		if (std::find(stockfishMoves.begin(), stockfishMoves.end(), movePair) == stockfishMoves.end()){
			differentMoves.push_back(std::make_tuple(movePair.first, movePair.second, MoveSource::Thera));
		}

	}
	
	for (auto movePair : stockfishMoves){
		if (std::find(theraMoves.begin(), theraMoves.end(), movePair) == theraMoves.end()){
			differentMoves.push_back(std::make_tuple(movePair.first, movePair.second, MoveSource::Stockfish));
		}
	}

	std::sort(differentMoves.begin(), differentMoves.end());

	for (auto [move, numSubmoves, source] : differentMoves){
		message += indentation + std::string("[") + (source == MoveSource::Thera ? "Thera]     " : "Stockfish] ");
		message += Thera::Utils::squareToAlgebraicNotation(move.startIndex) + Thera::Utils::squareToAlgebraicNotation(move.endIndex);
		switch (move.promotionType){
			case Thera::PieceType::Bishop: message += "b"; break;
			case Thera::PieceType::Knight: message += "n"; break;
			case Thera::PieceType::Rook:   message += "r"; break;
			case Thera::PieceType::Queen:  message += "q"; break;
			default: break;
		}
		message += ": " + std::to_string(numSubmoves) + "\n";
		if (userInput.perftDepth > 0){
			auto moveIt = std::find_if(theraMoves.begin(), theraMoves.end(), [move = move](auto other){return Thera::Move::isSameBaseMove(move, other.first);});
			if (moveIt == theraMoves.end()){
				message += indentation + "\tMove not found!\n";
			}
			else{
				MoveInputResult newUserInput = userInput;
				newUserInput.perftDepth--;
				board.applyMove(moveIt->first);
				analyzePosition(newUserInput, board, generator, message, originalDepth);
				board.rewindMove();
			}
		}
	}

	// find moves that got generated twice
	std::sort(theraMovesRaw.begin(), theraMovesRaw.end());
	auto duplicate = std::adjacent_find(theraMovesRaw.begin(), theraMovesRaw.end());
	while (duplicate != theraMovesRaw.end()){
		message += indentation + std::string("[Thera]     ");
		message += Thera::Utils::squareToAlgebraicNotation(duplicate->startIndex) + Thera::Utils::squareToAlgebraicNotation(duplicate->endIndex);
		switch (duplicate->promotionType){
			case Thera::PieceType::Bishop: message += "b"; break;
			case Thera::PieceType::Knight: message += "n"; break;
			case Thera::PieceType::Rook:   message += "r"; break;
			case Thera::PieceType::Queen:  message += "q"; break;
			default: break;
		}
		message += ": " + ANSI::set4BitColor(ANSI::Red) + "Duplicate!" + ANSI::reset() + "\n";
		duplicate = std::adjacent_find(std::next(duplicate), theraMovesRaw.end());
	}

	if (userInput.perftDepth != originalDepth) return;

	message += ANSI::set4BitColor(ANSI::Blue);
	message += indentation + "Stockfish searched " + std::to_string(stockfishMoves.size()) + " moves (" + std::to_string(stockfishNodesSearched) + " nodes)\n";
	message += indentation + "Thera searched " + std::to_string(theraMoves.size()) + " moves (" + std::to_string(theraNodesSearched) + " nodes)\n";

	message += "Filtered " + std::to_string(filteredMoves) + " moves\n";

	message += indentation + "Results are ";
	if (differentMoves.size() || theraMoves.size() != stockfishMoves.size())
		message += indentation + ANSI::set4BitColor(ANSI::Red) + "different" + ANSI::set4BitColor(ANSI::Blue) +".\n";
	else
		message += indentation + ANSI::set4BitColor(ANSI::Green) + "identical" + ANSI::set4BitColor(ANSI::Blue) +".\n";
}

int playMode(Options& options){
	Thera::Board board;
	board.loadFromFEN(options.fen);

	std::array<RGB, 64> highlights;
	highlights.fill(RGB());
	options.shownPieceBitboard = Thera::Piece(Thera::PieceType::None, Thera::PieceColor::White);
	options.selectedBitboard = BitboardSelection::None;

	Thera::MoveGenerator generator;

	std::string message =  	"Enter move or type 'exit'.\n"
							"Change your move by typing 'change'.\n"
							"Undo last move using 'undo'.";
	while (true){
		MoveInputResult userInput;

		redrawGUI(options, board, generator, highlights, message);
		message = "";
		
		getUserMoveStart(userInput, options);
		
		if (userInput.failed){
			message = ANSI::set4BitColor(ANSI::Red) + userInput.message + ANSI::reset();
			continue;
		}

		if (userInput.op == MoveInputResult::Exit) break;
		else if (userInput.op == MoveInputResult::UndoMove){
			try{
				board.rewindMove();
				message = ANSI::set4BitColor(ANSI::Blue) + "Undid move." + ANSI::reset();
			}
			catch(std::runtime_error){
				message = ANSI::set4BitColor(ANSI::Red) + "No move to undo." + ANSI::reset();
			}
			continue;
		}
		else if (userInput.op == MoveInputResult::LoadFEN){
			board.loadFromFEN(options.fen);
			message = ANSI::set4BitColor(ANSI::Blue) + "Loaded position from FEN." + ANSI::reset();
			continue;
		}
		else if (userInput.op == MoveInputResult::Analyze){
			analyzePosition(userInput, board, generator, message, userInput.perftDepth);
			continue;
		}
		else if (userInput.op == MoveInputResult::FlipColors){
			board.switchPerspective();
			message = ANSI::set4BitColor(ANSI::Blue) + "Flipped color to move." + ANSI::reset();
			continue;
		}
		else if (userInput.op == MoveInputResult::Perft){
			const auto messageLoggingMovePrint = [&](Thera::Move const& move, int numSubmoves){
				message
					+= Thera::Utils::squareToAlgebraicNotation(move.startIndex)
					+  Thera::Utils::squareToAlgebraicNotation(move.endIndex);
				switch (move.promotionType){
					case Thera::PieceType::Bishop: message += "b"; break;
					case Thera::PieceType::Knight: message += "n"; break;
					case Thera::PieceType::Rook:   message += "r"; break;
					case Thera::PieceType::Queen:  message += "q"; break;
					default: break;
				}
				message += ": " + std::to_string(numSubmoves) + "\n";
			};

			message = "";

			int filteredMoves = 0;
			int nodesSearched = Thera::perft(board, generator, userInput.perftDepth, true, messageLoggingMovePrint, filteredMoves);

			// write the perft output to a file for easier debugging
			std::ofstream logFile("/tmp/thera.txt", std::ofstream::trunc);
			if (!logFile.is_open()){
				message += ANSI::set4BitColor(ANSI::Red) + "Unable to open logfile! Ignoring." + ANSI::reset(ANSI::Foreground) + "\n";
			}
			else{
				logFile << message;
				logFile.close();
			}

			message += "Filtered " + std::to_string(filteredMoves) + " moves\n";
			message += "Nodes searched: " + std::to_string(nodesSearched) + "\n";

			continue;
		}
		else if (userInput.op == MoveInputResult::Continue)
			continue;

		auto const isCorrectStart = [&](auto const& move){
			return move.startIndex == userInput.move.startIndex;
		};

		std::vector<Thera::Move> allMoves = generator.generateAllMoves(board);
		std::vector<Thera::Move> possibleMoves;
		std::copy_if(allMoves.begin(), allMoves.end(), std::back_inserter(possibleMoves), isCorrectStart);

		message = "";

		for (auto move : possibleMoves){
			message	+= ""
					+  Thera::Utils::squareToAlgebraicNotation(move.startIndex)
					+  Thera::Utils::squareToAlgebraicNotation(move.endIndex);
			switch (move.promotionType){
				case Thera::PieceType::Bishop: message += "b"; break;
				case Thera::PieceType::Knight: message += "n"; break;
				case Thera::PieceType::Rook:   message += "r"; break;
				case Thera::PieceType::Queen:  message += "q"; break;
				default: break;
			}	
			message += "\n";
		}
		message += ANSI::set4BitColor(ANSI::Blue) + "Number of moves: " + std::to_string(possibleMoves.size());
		

		if (options.selectedBitboard == BitboardSelection::None){
			// highlight all moves
			highlights.at(userInput.move.startIndex.getIndex64()) = highlightSquareSelected;
			for (auto const& move : possibleMoves){
				highlights.at(move.endIndex.getIndex64()) = highlightMovePossible;
			}
		}

		redrawGUI(options, board, generator, highlights, message);
		
		getUserMoveEnd(userInput, options);
		if (userInput.op == MoveInputResult::Exit) break;
		else if (userInput.op == MoveInputResult::Continue) continue;
		else if (userInput.op == MoveInputResult::ForceMove){
			if (board.getColorToMove() == board.at(userInput.move.startIndex).getColor())
				board.applyMove(userInput.move);
			else
				board.applyMoveStatic(userInput.move);
			message = ANSI::set4BitColor(ANSI::Blue) + "Forced move." + ANSI::reset();
		}
		else if (userInput.op == MoveInputResult::UndoMove){
			board.rewindMove();
			message = ANSI::set4BitColor(ANSI::Blue) + "Undone move." + ANSI::reset();
		}
		else{
			auto moveIt = std::find_if(possibleMoves.begin(), possibleMoves.end(), [&](auto other){return Thera::Move::isSameBaseMove(userInput.move, other);});
			if (moveIt != possibleMoves.end()){
				// apply the found move since the user move
				// won't have any stats attached
				board.applyMove(*moveIt);
			}
			else{
				message = ANSI::set4BitColor(ANSI::Red) + "Invalid move!" + ANSI::reset();
			}
		}
	}

	std::cout << ANSI::reset() << "Bye...\n";
	return 0;
}