const fs = require("fs");
const stockCandleDirectoryResults = "STOCKCANDLES_RESULTS";
let sumOfDelaysForCandleStickData = 0;
let sumOfDelaysForMovingMean = 0;

module.exports = {
  makeDir(dirName) {
    fs.mkdir(dirName, { recursive: true }, (err) => {
      if (err) throw err;
    });
  },
  writeContentToFile(content, fileName) {
    if (!fs.existsSync(fileName)) {
      let writer = fs.createWriteStream(fileName);
      writer.write(content);
    } else {
      fs.appendFileSync(fileName, content);
    }
  },

  getDateFromTimestamp(unix_timestamp) {
    var date = new Date(unix_timestamp);

    return date.toTimeString();
  },
  getKeysFromMap(map) {
    const iterator = map.keys();

    return Array.from(iterator);
  },
  getMovingMeanEveryMinute(movingMeanFilename, timestampToStockValuesMap) {
    // this function writes one entry per minute
    var start = Date.now();

    // map me timestamp -> array with stock values , diagrafoume to mikrotero timestamp kathe 15 minutes
    // find sum and length of values array for each minute we have stored
    const mean = module.exports.getMeanOfStockValuesForEachMinute(
      timestampToStockValuesMap
    );
    const timestamps = module.exports.getKeysFromMap(timestampToStockValuesMap);

    const fileContentForMean = `Mean for ${
      timestamps.length
    } stock values is ${mean.toString()}\n`;
    module.exports.writeContentToFile(fileContentForMean, movingMeanFilename);

    // if we have calculated mean for more than 15 minutes,
    // we remove the smallest minute from timestampToStockValuesMap map
    if (timestamps.length > 15) {
      console.log(
        "We passed 15 minutes. We will remove the smallest minute...............\n"
      );

      console.log(`Initial minutes available: ${timestamps}........`);

      const min = Math.min.apply(Math, timestamps);
      console.log(`I will remove stock values from minute ${min} ........`);

      timestampToStockValuesMap.delete(min);
      console.log(
        `Minutes available after removal:  
      ${module.exports.getKeysFromMap(timestampToStockValuesMap)}........`
      );

      // we calculate delay for writing to file for moving mean
      var end = Date.now();
      var delay = (end - start) * 1000; //μs
      sumOfDelaysForMovingMean += delay;
      module.exports.writeContentToFile(
        `sum of delays is ${sumOfDelaysForMovingMean}μs\n`,
        "delaysFor3Question.txt"
      );
    }
    return timestampToStockValuesMap;
  },

  getMeanOfStockValuesForEachMinute(timestampToStockValuesMap) {
    let sum = 0;
    let length = 0;
    timestampToStockValuesMap.forEach((values, timestamp) => {
      const sumOfStockValuesForMinute = values.reduce(
        (partialSum, a) => partialSum + a,
        0
      );
      const lengthForMinute = values.length;
      sum += sumOfStockValuesForMinute;
      length += lengthForMinute;
    });

    return sum / length;
  },

  saveStockCandlesFromCompany(
    finnhubClient,
    companySymbol,
    currentTimeTimestamp,
    beforeAMinuteTimestamp
  ) {
    finnhubClient.stockCandles(
      companySymbol,
      "D",
      beforeAMinuteTimestamp,
      currentTimeTimestamp,
      (error, data, response) => {
        let fileName = stockCandleDirectoryResults + "/" + companySymbol;
        fileName = fileName.replace(":", "");
        fileName = fileName + "_STOCKCANDLES.txt";
        const dateTime =
          module.exports.getDateFromTimestamp(currentTimeTimestamp);
        const content = dateTime + " " + JSON.stringify(data) + "\n";
        module.exports.writeContentToFile(content, fileName);
      }
    );
  },
  getStockCandlesForSymbols(finnhubClient, symboles) {
    var newTimestamp = Date.now();
    var aMinuteAgo = new Date(newTimestamp);
    var durationInMinutes = 1;
    // one min behind too newTimestamp - 60 * 1000; // milisseconds
    const oldTimestamp = aMinuteAgo.setMinutes(
      aMinuteAgo.getMinutes() - durationInMinutes
    );

    console.log(
      "A minute has passed. I am getting the stock candles for each symbol...."
    );

    symboles.forEach((symbol) =>
      module.exports.saveStockCandlesFromCompany(
        finnhubClient,
        symbol,
        newTimestamp,
        oldTimestamp
      )
    );

    // we calculate delay for writing to file for candlesticks
    var end = Date.now();
    var delay = (end - newTimestamp) * 1000; //μs
    sumOfDelaysForCandleStickData += delay;
    module.exports.writeContentToFile(
      `sum of delays is ${sumOfDelaysForCandleStickData}μs\n`,
      "delaysFor2Question.txt"
    );
    return newTimestamp;
  },
};
