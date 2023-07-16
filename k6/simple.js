import http from 'k6/http';

// export const options = {
//   stages: [
//     {duration: '5s', target: 100},
//     {duration: '5s', target: 200},
//     {duration: '5s', target: 300},
//     {duration: '5s', target: 400},
//     {duration: '5s', target: 500},
//     {duration: '5s', target: 600},
//     {duration: '5s', target: 700},
//     {duration: '5s', target: 800},
//     {duration: '5s', target: 900},
//     {duration: '5s', target: 1000},
//     {duration: '85s', target: 1000},
//     {duration: '5s', target: 900},
//     {duration: '5s', target: 800},
//     {duration: '5s', target: 700},
//     {duration: '5s', target: 600},
//     {duration: '5s', target: 500},
//     {duration: '5s', target: 400},
//     {duration: '5s', target: 300},
//     {duration: '5s', target: 200},
//     {duration: '5s', target: 100},
//   ]
// }

export const options = {
  stages: [
    { duration: '5s', target: 100 },
    { duration: '5s', target: 200 },
    { duration: '5s', target: 300 },
    { duration: '5s', target: 400 },
    { duration: '5s', target: 500 },
    { duration: '45s', target: 500 },
    { duration: '5s', target: 400 },
    { duration: '5s', target: 300 },
    { duration: '5s', target: 200 },
    { duration: '5s', target: 100 },
  ],
  summaryTrendStats: ['min', 'avg', 'med', 'max', 'p(99)', 'p(99.9)', 'p(99.99)'],
}

export default function() {
  http.get('http://127.0.0.1:8000');
}
