const utils = require("./utilities.js");
const finnhub = require("finnhub");

const appleSymbol = "AAPL";
const ibmSymbol = "IBM";
const binanceBtcSymbol = "BINANCE:BTCUSDT";
const icMarketSymbol = "IC MARKETS:1";
const symboles = [appleSymbol, ibmSymbol, binanceBtcSymbol, icMarketSymbol];

let sumOfDelaysForStockValues = 0;

const fileNameForDelaysFor1Question = "delaysFor1Question.txt";

const apiKey = "ca5l84iad3i4sbn0eskg";
const api_key = finnhub.ApiClient.instance.authentications["api_key"];
api_key.apiKey = apiKey;

const finnhubClient = new finnhub.DefaultApi();

// array with maps for each symbol. Each map contains minute to values taken at that minute (map)
const movingMeansStocks = new Map();

// 1) get transactions for each symbol and write them down in a file, named after the symbol
const WebSocket = require("ws");
const { writeContentToFile } = require("./utilities.js");
const stockCandleDirectoryResults = "STOCKCANDLES_RESULTS";
const movingMeansDirectoryResults = "MOVINGMEANS_RESULTS";
const stocksRealTimeValues = "STOCKSDATA_RESULTS";
const directoryNames = [
  stockCandleDirectoryResults,
  movingMeansDirectoryResults,
  stocksRealTimeValues,
];
directoryNames.forEach((directoryName) => utils.makeDir(directoryName));
const socket = new WebSocket("wss://ws.finnhub.io?token=" + apiKey);

socket.addEventListener("open", function (event) {
  symboles.forEach((symbol) =>
    socket.send(JSON.stringify({ type: "subscribe", symbol: symbol }))
  );
});

// Listen for messages
socket.addEventListener("message", function (event) {
  var start = Date.now();
  var dataObject = JSON.parse(event.data);
  const fileContent = event.data + "\n";
  if (!dataObject["data"]) return;
  const symbolValue = dataObject["data"][0]["s"].replace(":", "");
  let stockValue = dataObject["data"][0]["p"];
  let volumeValue = dataObject["data"][0]["v"];
  let timestampValue = dataObject["data"][0]["t"];

  const fileNameWithSuffix = "/" + symbolValue + ".txt";
  const fileName = stocksRealTimeValues + fileNameWithSuffix;

  if (!movingMeansStocks.has(symbolValue)) {
    movingMeansStocks.set(symbolValue, new Map());
  }
  const timestampToStockValuesMap = movingMeansStocks.get(symbolValue); // timestamp string, array me values

  const d = new Date(Date.now());
  const currentMinute = d.getMinutes();
  if (!timestampToStockValuesMap.has(currentMinute)) {
    timestampToStockValuesMap.set(currentMinute, []);
  }
  timestampToStockValuesMap.get(currentMinute).push(stockValue);

  utils.writeContentToFile(
    `${symbolValue}: Last price: ${stockValue}, timestamp: ${timestampValue}, volume: ${volumeValue}\n`,
    fileName
  );

  // we calculate delay for writing to file for stock values
  var end = Date.now();
  var delay = (end - start) * 1000; //μs
  sumOfDelaysForStockValues += delay;
  const dateTime = utils.getDateFromTimestamp(end);
  writeContentToFile(
    `${dateTime} sum of delays is ${sumOfDelaysForStockValues}μs\n`,
    fileNameForDelaysFor1Question
  );
});

// 2) calculate candlestick for each symbol each minute and save it to file
// every minute, we ask for the candlestick information for each symbol
setInterval(function () {
  oldTimestmap = utils.getStockCandlesForSymbols(finnhubClient, symboles);
}, 60000); //60000 -> 60 seconds -> 1 minute, 11min: 60000 * 11

// each minute we calculate the mean from the created map. If minutes pass 15, we
// delete oldest minute and it's data
setInterval(function () {
  symboles.forEach((symbol) => {
    const symbolName = symbol.replace(":", "");

    const fileNameWithSuffix = "/" + symbolName + ".txt";
    const movingMeanFilename = movingMeansDirectoryResults + fileNameWithSuffix;

    let timestampToStockValuesMap = movingMeansStocks.get(symbolName);

    if (!timestampToStockValuesMap) {
      // we don't have stock values for that symbol yet
      return;
    }
    timestampToStockValuesMap = utils.getMovingMeanEveryMinute(
      movingMeanFilename,
      timestampToStockValuesMap
    );

    movingMeansStocks.set(symbolName, timestampToStockValuesMap);
  });
}, 60000); //60000 -> 60 seconds -> 1 minute
